// Diagnostics of the device for the Android host (plan item 301c). dm_thermal_now answers Apple's thermal states (0 nominal ... 3 critical, diag.c's
// performance lines and the hold of 30 fps when hot): not read here yet; how Android's thermal status levels map onto those four is measured with the
// frame rate work on the phone (301f), so this says nominal until then.
#include "dm.h"

int dm_thermal_now(void) { return 0; }
