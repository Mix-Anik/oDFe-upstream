/*
The part of the engine API the SDK library itself uses (math and bulk memory, which both kinds of plugin get from common.api).
The build script defines ODF_MODULE_CLIENT or ODF_MODULE_SERVER; the tests of the SDK define ODF_MODULE_TEST.
*/

#ifndef ODF_SDK_API_H
#define ODF_SDK_API_H

#if defined( ODF_MODULE_CLIENT )
#include "odfc.h"
#elif defined( ODF_MODULE_SERVER )
#include "odfs.h"
#elif defined( ODF_MODULE_TEST )
#include "odft.h"
#else
#error define ODF_MODULE_CLIENT or ODF_MODULE_SERVER (the build script does)
#endif

#endif
