#ifndef __GX_RC_H__
#define __GX_RC_H__

#include "platform.h"

GX_NS_BEGIN

enum {
    GX_LOGIC_RC = 128,
    GX_EFAIL = GX_LOGIC_RC,
    GX_EDUP,
    GX_EEXISTS,
    GX_ENOTEXISTS,
    GX_EREADY,
    GX_ENOTREADY,
    GX_ELESS,
    GX_EMORE,
    GX_EPARAM,
    GX_EAGAIN,
    GX_ESYS_RC,
    GX_ETIMEOUT,
    GX_ECLOSED,
    GX_ECLOSE,
    GX_EBUSY,
    GX_ESYS_END,
};

GX_NS_END

#endif
