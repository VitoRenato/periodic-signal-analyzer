#include "calibration_export.h"

#include <stddef.h>

void CalibrationExport_Init(CalibrationExport *export_state)
{
  if (export_state != NULL)
  {
    export_state->capturing = 0u;
  }
}

CalExportAction CalibrationExport_Request(
    CalibrationExport *export_state,
    bool acquiring)
{
  if (export_state == NULL)
  {
    return CAL_EXPORT_NONE;
  }
  if (export_state->capturing != 0u)
  {
    return CAL_EXPORT_NONE;
  }

  export_state->capturing = 1u;
  return acquiring ? CAL_EXPORT_NONE : CAL_EXPORT_START_CAPTURE;
}

CalExportAction CalibrationExport_OnFrameDone(CalibrationExport *export_state)
{
  if ((export_state == NULL) || (export_state->capturing == 0u))
  {
    return CAL_EXPORT_NONE;
  }

  export_state->capturing = 0u;
  return CAL_EXPORT_EMIT;
}

bool CalibrationExport_IsCapturing(const CalibrationExport *export_state)
{
  return (export_state != NULL) && (export_state->capturing != 0u);
}
