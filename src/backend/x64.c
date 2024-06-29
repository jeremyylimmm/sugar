#include "sb.h"
#include "sb_internal.h"

void sb_generate_x64(SB_Context* context, SB_Proc* proc) {
    Scratch scratch = scratch_get(&context->scratch_library, 0, 0);

    global_code_motion(scratch.arena, context, proc);

    scratch_release(&scratch);
}