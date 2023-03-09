#include "qemu/osdep.h"
#include "exec/gdbstub.h"
#include "gdbstub/internals.h"

bool gdb_query_capa_read_supported(void)
{
    return false;
}

void gdb_handle_query_xfer_capa_read(GArray *params, void *user_ctx)
{
    gdb_put_packet("");
}
