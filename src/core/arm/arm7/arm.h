#pragma once

#include <stddef.h>
#include "../../utils.h"
#include "../arm_shared/arm.h"



// exact model: ARM7TDMI (idk if there were revisions?)

struct ARM7TDMI
{
    struct ARM ARM;
    bool CodeSeq; // should the next code fetch be sequential
};

// ensure casting between the two types works as expected
static_assert(offsetof(struct ARM7TDMI, ARM) == 0);
