#include "calibration_export.h"

#include <assert.h>
#include <stdio.h>

static void test_frozen_request_starts_capture_without_touching_session(void)
{
  CalibrationExport state;

  CalibrationExport_Init(&state);
  assert(!CalibrationExport_IsCapturing(&state));
  assert(CalibrationExport_Request(&state, false) == CAL_EXPORT_START_CAPTURE);
  assert(CalibrationExport_IsCapturing(&state));
  assert(CalibrationExport_OnFrameDone(&state) == CAL_EXPORT_EMIT);
  assert(!CalibrationExport_IsCapturing(&state));
  assert(CalibrationExport_OnFrameDone(&state) == CAL_EXPORT_NONE);
}

static void test_acquiring_request_uses_inflight_frame(void)
{
  CalibrationExport state;

  CalibrationExport_Init(&state);
  assert(CalibrationExport_Request(&state, true) == CAL_EXPORT_NONE);
  assert(CalibrationExport_IsCapturing(&state));
  assert(CalibrationExport_Request(&state, true) == CAL_EXPORT_NONE);
  assert(CalibrationExport_OnFrameDone(&state) == CAL_EXPORT_EMIT);
}

static void test_no_implicit_restart(void)
{
  CalibrationExport state;

  CalibrationExport_Init(&state);
  assert(CalibrationExport_OnFrameDone(&state) == CAL_EXPORT_NONE);
  assert(!CalibrationExport_IsCapturing(&state));
}

int main(void)
{
  test_frozen_request_starts_capture_without_touching_session();
  test_acquiring_request_uses_inflight_frame();
  test_no_implicit_restart();
  puts("calibration_export_tests: PASS");
  return 0;
}
