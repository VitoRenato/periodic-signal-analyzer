#include "signal_analyzer.h"
#include "signal_metrics.h"
#include "h743_measurement_pages.h"
#include "h743_measurement_rx.h"
#include <math.h>
#include <stdio.h>
#include <stdint.h>

#define PI 3.14159265358979323846
#define FS_HZ 2048780U
/* These codes are synthesized from reported display values, not ADC captures. */
#define FIXED_V_PER_CODE 0.00241129
static uint16_t samples[SIGNAL_FRAME_SAMPLES];
static unsigned failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); ++failures; } } while (0)

static void near_value(const char *name, double expected, double actual, double tolerance)
{
    if (!isfinite(actual) || fabs(expected - actual) > tolerance)
    {
        fprintf(stderr,"FAIL %s expected=%.9f actual=%.9f tolerance=%.9f\n",name,expected,actual,tolerance);
        ++failures;
    }
}

static void synth(const double *freq, const double *peak, unsigned count, uint32_t fs)
{
    for (unsigned i=0; i<SIGNAL_FRAME_SAMPLES; ++i)
    {
        double voltage=0.0;
        for (unsigned k=0; k<count; ++k)
            voltage+=peak[k]*cos(2.0*PI*freq[k]*i/fs+0.31+0.29*k);
        samples[i]=(uint16_t)(2055L+lround(voltage/FIXED_V_PER_CODE));
    }
}

static void reported_bench_points(void)
{
    FILE *file=fopen(FRONTEND_BENCH_FIXTURE,"r");
    CHECK(file!=NULL);
    if (!file) return;
    char header[128];
    CHECK(fgets(header,sizeof(header),file)!=NULL);
    double frequency,nominal,displayed;
    unsigned count=0;
    double maximum_error=0.0;
    while (fscanf(file,"%lf,%lf,%lf",&frequency,&nominal,&displayed)==3)
    {
        const double peak=displayed*0.0005;
        SignalResult result;
        synth(&frequency,&peak,1U,FS_HZ);
        CHECK(SignalAnalyzer_ProcessCalibrated(samples,FS_HZ,&result)==SIGNAL_STATUS_VALID);
        CHECK(result.component_count==1U);
        near_value("bench frequency",frequency,result.fundamental_Hz,100.0);
        near_value("bench Vpp",nominal*0.001,result.upp_V,0.003);
        near_value("bench Vrms",nominal*0.001/(2.0*sqrt(2.0)),result.urms_V,0.0015);
        near_value("bench peak",nominal*0.0005,result.component[0].amplitude_peak_V,0.0015);
        const double error=fabs(result.upp_V*1000.0-nominal);
        if (error>maximum_error) maximum_error=error;
        ++count;
    }
    CHECK(feof(file));
    fclose(file);
    CHECK(count==41U);
    printf("reported-display-derived synthetic frames=%u max_error_mVpp=%.6f\n",count,maximum_error);
}

static void post_compensation_followup(void)
{
    FILE *file=fopen(FRONTEND_FOLLOWUP_FIXTURE,"r");
    CHECK(file!=NULL);
    if (!file) return;
    char header[128];
    CHECK(fgets(header,sizeof(header),file)!=NULL);
    /* Prior gains are frozen first-firmware metadata, independent of the new table. */
    double low,high,nominal,displayed,prior_low,prior_high;
    unsigned observations=0,cases=0;
    double maximum_error=0.0;
    while (fscanf(file,"%lf,%lf,%lf,%lf,%lf,%lf",&low,&high,&nominal,&displayed,&prior_low,&prior_high)==6)
    {
        const double endpoints[]={low,high};
        const double prior_gain[]={prior_low,prior_high};
        for (unsigned end=0;end<(high==low?1U:2U);++end)
        {
            const double f=endpoints[end];
            const double peak=displayed*0.0005/prior_gain[end];
            SignalResult result;
            synth(&f,&peak,1U,FS_HZ);
            CHECK(SignalAnalyzer_ProcessCalibrated(samples,FS_HZ,&result)==SIGNAL_STATUS_VALID);
            CHECK(result.component_count==1U);
            near_value("follow-up Vpp",nominal*0.001,result.upp_V,0.003);
            near_value("follow-up RMS",nominal*0.001/(2.0*sqrt(2.0)),result.urms_V,0.0015);
            near_value("follow-up peak",nominal*0.0005,result.component[0].amplitude_peak_V,0.0015);
            near_value("follow-up frequency",f,result.fundamental_Hz,100.0);
            const double error=fabs(result.upp_V*1000.0-nominal);
            if (error>maximum_error) maximum_error=error;
            ++cases;
        }
        ++observations;
    }
    CHECK(feof(file));
    fclose(file);
    CHECK(observations==8U && cases==10U);
    printf("post-compensation observations=%u synthetic endpoints=%u max_error_mVpp=%.6f\n",observations,cases,maximum_error);
}

static void interpolation_and_sampling_rates(void)
{
    /* Independent expectations for the six midpoints of the fitted node table. */
    const double frequency[]={30000,75000,150000,250000,350000,450000};
    const double gain[]={1.0547854353,1.0558476234,1.0340866802,1.0483875628,1.0685240236,1.1010727577};
    const uint32_t fs[]={2048780U,10500000U};
    const double peak=0.060;
    for (unsigned s=0;s<2;++s) for (unsigned k=0;k<6;++k)
    {
        SignalResult raw,cal;
        synth(&frequency[k],&peak,1U,fs[s]);
        CHECK(SignalAnalyzer_Process(samples,fs[s],&raw)==SIGNAL_STATUS_VALID);
        CHECK(SignalAnalyzer_ProcessCalibrated(samples,fs[s],&cal)==SIGNAL_STATUS_VALID);
        near_value("interpolated Vpp",raw.upp_V*gain[k],cal.upp_V,0.00001);
        near_value("frequency unchanged",raw.fundamental_Hz,cal.fundamental_Hz,0.0);
        near_value("phase unchanged",raw.component[0].phase_rad,cal.component[0].phase_rad,0.0);
        near_value("residual unchanged",raw.residual_rms_V,cal.residual_rms_V,0.0);
        near_value("SNR unchanged",raw.snr_dB,cal.snr_dB,0.0);
        CHECK(raw.quality_flags==cal.quality_flags);
        CHECK(raw.sample_rate_Hz==cal.sample_rate_Hz);
    }
}

static void protocol_round_trip(const SignalResult *result)
{
    H743MeasurementPager pager;
    H743MeasurementRx receiver;
    H743MeasurementModel model;
    uint8_t frame[H743_SET_FS_FRAME_SIZE];
    H743MeasurementPager_Init(&pager);
    H743MeasurementRx_Init(&receiver);
    H743MeasurementRx_SetRequestGeneration(&receiver,7U);
    CHECK(H743MeasurementPager_Load(&pager,result,result->sample_rate_Hz,result->sample_rate_Hz,7U));
    for (unsigned i=0;i<H743_MEASUREMENT_PAGE_COUNT;++i)
    {
        CHECK(H743MeasurementPager_EncodeNext(&pager,i==0U,frame));
        const bool completed=H743MeasurementRx_ProcessFrame(&receiver,frame,sizeof(frame));
        CHECK(completed==(i+1U==H743_MEASUREMENT_PAGE_COUNT));
    }
    CHECK(H743MeasurementRx_GetPublished(&receiver,&model));
    CHECK(model.component_count==result->component_count);
    CHECK(model.analysis_sample_rate_Hz==result->sample_rate_Hz);
    CHECK(model.request_generation==7U);
    near_value("wire Vpp",result->upp_V,model.upp_uV*1e-6,0.000501);
    near_value("wire RMS",result->urms_V,model.urms_uV*1e-6,0.000501);
    for (unsigned k=0;k<result->component_count;++k)
        near_value("wire peak",result->component[k].amplitude_peak_V,model.component[k].amplitude_peak_uV*1e-6,0.000501);
}

static void per_component_metrics(void)
{
    const double frequency[]={100000,300000,500000,1200000};
    const double peak[]={0.040,0.025,0.015,0.080};
    const double gain[]={1.0571217971,1.0556614896,1.1207589579};
    const uint32_t rates[]={FS_HZ,10500000U};
    for (unsigned s=0;s<2;++s)
    {
        SignalResult raw,cal;
        synth(frequency,peak,s?4U:3U,rates[s]);
        CHECK(SignalAnalyzer_Process(samples,rates[s],&raw)==SIGNAL_STATUS_VALID);
        CHECK(SignalAnalyzer_ProcessCalibrated(samples,rates[s],&cal)==SIGNAL_STATUS_VALID);
        CHECK(raw.component_count==3U && cal.component_count==3U);
        if (raw.component_count!=3U || cal.component_count!=3U) continue;
        double corrected_peak[3],power=0.0,minimum=1e9,maximum=-1e9;
        for (unsigned k=0;k<3;++k)
        {
            corrected_peak[k]=raw.component[k].amplitude_peak_V*gain[k];
            near_value("per-component peak",corrected_peak[k],cal.component[k].amplitude_peak_V,0.00001);
            near_value("component phase",raw.component[k].phase_rad,cal.component[k].phase_rad,0.0);
            near_value("component frequency",raw.component[k].frequency_Hz,cal.component[k].frequency_Hz,0.0);
            power+=corrected_peak[k]*corrected_peak[k]*0.5;
        }
        for (unsigned i=0;i<65536U;++i)
        {
            double value=0.0;
            for (unsigned k=0;k<3;++k)
                value+=corrected_peak[k]*cos(2.0*PI*(2U*k+1U)*i/65536.0+raw.component[k].phase_rad);
            if (value<minimum) minimum=value;
            if (value>maximum) maximum=value;
        }
        near_value("reconstructed Vpp",maximum-minimum,cal.upp_V,0.00002);
        near_value("recomputed RMS",sqrt(power),cal.urms_V,0.00001);
        near_value("residual stays raw",raw.residual_rms_V,cal.residual_rms_V,0.0);
        CHECK(raw.quality_flags==cal.quality_flags);
        CHECK(!s || (cal.quality_flags&SIGNAL_QUALITY_INTERFERENCE_REMOVED));
        protocol_round_trip(&cal);
    }
}

static void recovered_clip_and_unsafe_rate(void)
{
    const double frequency=100123.0, peak=0.110;
    SignalResult raw,cal;
    synth(&frequency,&peak,1U,FS_HZ);
    for (unsigned i=0;i<SIGNAL_FRAME_SAMPLES;++i)
    {
        samples[i]=(uint16_t)(samples[i]+15U);
        if (samples[i]<2048U) samples[i]=2048U;
    }
    CHECK(SignalAnalyzer_Process(samples,FS_HZ,&raw)==SIGNAL_STATUS_VALID);
    CHECK(SignalAnalyzer_ProcessCalibrated(samples,FS_HZ,&cal)==SIGNAL_STATUS_VALID);
    CHECK(cal.quality_flags&SIGNAL_QUALITY_CLIPPED_MODEL_RECOVERED);
    CHECK(cal.quality_flags==raw.quality_flags);
    const double gain=1.05712180+(1.028172878-1.05712180)*0.123;
    near_value("recovered clip gain",raw.upp_V*gain,cal.upp_V,0.00001);
    near_value("recovered clip phase",raw.component[0].phase_rad,cal.component[0].phase_rad,0.0);
    synth(&frequency,&peak,1U,500000U);
    CHECK(SignalAnalyzer_Process(samples,500000U,&raw)==SIGNAL_STATUS_SAMPLE_RATE_UNSAFE);
    CHECK(SignalAnalyzer_ProcessCalibrated(samples,500000U,&cal)==SIGNAL_STATUS_SAMPLE_RATE_UNSAFE);
    CHECK(cal.recommended_sample_rate_Hz==raw.recommended_sample_rate_Hz);
    near_value("unsafe-rate correction",raw.upp_V*gain,cal.upp_V,0.00001);
}

static void quality_gate_is_before_correction(void)
{
    HarmonicModel model={.tone_count=1,.frequency_Hz={500000},.desired={1}};
    OrthogonalFitResult fit={.tone={{.frequency_Hz=500000,.amplitude_peak_V=0.020f}},.residual_rms_V=0.0046,.snr_dB=30.0};
    SignalResult result={0};
    CHECK(!SignalMetrics_Populate(&model,&fit,&result));
    CHECK(!SignalMetrics_PopulateCalibrated(&model,&fit,&result));
    CHECK(!SignalMetrics_PopulateCalibrated(NULL,&fit,&result));
    CHECK(!SignalMetrics_PopulateCalibrated(&model,NULL,&result));
    CHECK(!SignalMetrics_PopulateCalibrated(&model,&fit,NULL));
    CHECK(SignalAnalyzer_ProcessCalibrated(NULL,FS_HZ,&result)==SIGNAL_STATUS_BAD_ARGUMENT);
    CHECK(SignalAnalyzer_ProcessCalibrated(samples,FS_HZ,NULL)==SIGNAL_STATUS_BAD_ARGUMENT);
    for (unsigned i=0;i<SIGNAL_FRAME_SAMPLES;++i) samples[i]=2055U;
    CHECK(SignalAnalyzer_ProcessCalibrated(samples,FS_HZ,&result)==SIGNAL_STATUS_NO_SIGNAL);
}

int main(void)
{
    reported_bench_points();
    post_compensation_followup();
    interpolation_and_sampling_rates();
    per_component_metrics();
    recovered_clip_and_unsafe_rate();
    quality_gate_is_before_correction();
    printf("frontend_response_tests failures=%u\n",failures);
    return failures?1:0;
}
