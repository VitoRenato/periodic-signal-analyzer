#include "signal_analyzer.h"
#include "harmonic_model.h"
#include "orthogonal_fit.h"
#include "signal_metrics.h"
#include <math.h>
#include <stdio.h>

#define PI 3.14159265358979323846
static uint16_t samples[SIGNAL_FRAME_SAMPLES];
static unsigned failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); ++failures; } } while (0)

static void synth(const double *frequency, const double *peak, unsigned count, uint32_t fs)
{
    for (unsigned i=0;i<SIGNAL_FRAME_SAMPLES;++i)
    {
        double v=0.0;
        for (unsigned k=0;k<count;++k)
            v+=peak[k]*sin(2.0*PI*frequency[k]*(double)i/(double)fs+0.31+0.17*k);
        samples[i]=(uint16_t)(2055L+lround(v/0.00241129));
    }
}

static void test_dominant_tone_is_not_discarded(void)
{
    const double frequency[]={400000,10000,20000,30000};
    const double peak[]={0.100,0.010,0.010,0.010};
    SignalResult r;
    synth(frequency,peak,4,2048780U);
    CHECK(SignalAnalyzer_Process(samples,2048780U,&r)==SIGNAL_STATUS_VALID);
    unsigned found=0;
    for (unsigned k=0;k<r.component_count;++k)
        if (fabs(r.component[k].frequency_Hz-400000.0)<100.0 && fabs(r.component[k].amplitude_peak_V-0.100)<0.003) found=1;
    CHECK(found);
    CHECK(r.snr_dB>=10.0f);
    printf("dominant: f0=%.3f vpp=%.3fmV snr=%.3fdB components=%u\n",(double)r.fundamental_Hz,r.upp_V*1000.0,(double)r.snr_dB,(unsigned)r.component_count);
}

static void test_bad_fit_is_not_a_valid_measurement(void)
{
    HarmonicModel model={.tone_count=1,.frequency_Hz={10000},.desired={1}};
    OrthogonalFitResult fit={.tone={{.frequency_Hz=10000,.amplitude_peak_V=0.014f}},.residual_rms_V=0.070,.snr_dB=0.13};
    SignalResult result={0};
    CHECK(!SignalMetrics_Populate(&model,&fit,&result));
    model.tone_count=2;
    model.frequency_Hz[1]=1200000.0f;
    model.desired[1]=0;
    fit.tone[0].amplitude_peak_V=0.002f;
    fit.tone[1].frequency_Hz=1200000.0f;
    fit.tone[1].amplitude_peak_V=0.100f;
    fit.residual_rms_V=0.010;
    fit.snr_dB=17.0;
    CHECK(!SignalMetrics_Populate(&model,&fit,&result));
}

static void test_clean_band_edges_and_small_signal(void)
{
    const double frequencies[]={10000,400000,500000};
    const double peaks[]={0.025,0.125};
    for (unsigned f=0;f<3;++f) for (unsigned a=0;a<2;++a)
    {
        SignalResult r;
        synth(&frequencies[f],&peaks[a],1,2048780U);
        CHECK(SignalAnalyzer_Process(samples,2048780U,&r)==SIGNAL_STATUS_VALID);
        CHECK(fabs(r.fundamental_Hz-frequencies[f])<100.0);
        CHECK(fabs(r.upp_V-2.0*peaks[a])<0.002);
        CHECK(r.component_count==1);
    }
}

static void test_weak_fundamental_and_high_order_are_legal(void)
{
    const double frequency[]={10000,400000};
    const double peak[]={0.006,0.080};
    SignalResult r;
    synth(frequency,peak,2,2048780U);
    CHECK(SignalAnalyzer_Process(samples,2048780U,&r)==SIGNAL_STATUS_VALID);
    CHECK(fabs(r.fundamental_Hz-10000.0)<100.0);
    CHECK(r.component_count==2);
    CHECK(fabs(r.component[0].amplitude_peak_V-0.006)<0.001);
    CHECK(fabs(r.component[1].amplitude_peak_V-0.080)<0.001);
}

static void test_three_tones_and_out_of_band_interference(void)
{
    const double frequency[]={10500,31500,42000,1200000};
    const double peak[]={0.006,0.040,0.030,0.100};
    SignalResult r;
    synth(frequency,peak,4,10500000U);
    CHECK(SignalAnalyzer_Process(samples,10500000U,&r)==SIGNAL_STATUS_VALID);
    CHECK(fabs(r.fundamental_Hz-10500.0)<100.0);
    CHECK(r.component_count==3);
    CHECK((r.quality_flags&SIGNAL_QUALITY_INTERFERENCE_REMOVED)!=0);
    CHECK(fabs(r.urms_V-sqrt((0.006*0.006+0.040*0.040+0.030*0.030)/2.0))<0.001);
}

int main(void)
{
    test_dominant_tone_is_not_discarded();
    test_bad_fit_is_not_a_valid_measurement();
    test_clean_band_edges_and_small_signal();
    test_weak_fundamental_and_high_order_are_legal();
    test_three_tones_and_out_of_band_interference();
    printf("model_quality_tests: failures=%u\n",failures);
    return failures?1:0;
}
