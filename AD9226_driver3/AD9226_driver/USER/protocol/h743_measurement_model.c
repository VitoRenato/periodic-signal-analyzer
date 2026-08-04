#include "h743_measurement_model.h"

#include <stddef.h>

#define H743_MEASUREMENT_STATUS_VALID 0U

bool H743MeasurementModel_IsDisplayable(const H743MeasurementModel *model)
{
    size_t index;

    if ((model == NULL)
        || (model->status != H743_MEASUREMENT_STATUS_VALID)
        || (model->fundamental_Hz == 0U)
        || (model->upp_uV == 0U)
        || (model->urms_uV == 0U)
        || (model->component_count == 0U)
        || (model->component_count > H743_MEASUREMENT_MAX_COMPONENTS))
    {
        return false;
    }

    for (index = 0U; index < H743_MEASUREMENT_MAX_COMPONENTS; ++index)
    {
        const H743MeasurementComponent *component = &model->component[index];

        if (index < (size_t)model->component_count)
        {
            if ((component->valid == 0U)
                || (component->order == 0U)
                || (component->amplitude_peak_uV == 0U)
                || ((index == 0U) && (component->order != 1U))
                || ((index > 0U)
                    && (component->order
                        <= model->component[index - 1U].order)))
            {
                return false;
            }
        }
        else if (component->valid != 0U)
        {
            return false;
        }
    }
    return true;
}
