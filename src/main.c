#include "core/app.h"
#include "core/log.h"

/* Entry point: initialise AppContext, run main loop, shut down.
 * All engine state lives on the stack-owned `app` (no globals).
 *
 * Returns: 0 on success, 1 on init failure.
 */
int main(void)
{
    AppContext app;

    if (app_init(&app, 1280, 720, "TerraCraft") != 0) {
        LOG_ERROR("Failed to initialise TerraCraft. See logs above.");
        return 1;
    }

    int run_rc = app_run(&app);
    app_shutdown(&app);
    return run_rc == 0 ? 0 : 1;
}
