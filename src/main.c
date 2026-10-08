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

#ifdef NDEBUG
    /* Release ships quiet: per-chunk mesher chatter and other hot-path
     * DEBUG lines stay a debug-build tool, not a shipped log firehose. */
    log_set_level(LOG_LEVEL_INFO);
#endif

    if (app_init(&app, 1280, 720, "TerraCraft") != 0) {
        LOG_ERROR("Failed to initialise TerraCraft. See logs above.");
        return 1;
    }

    int run_rc = app_run(&app);
    app_shutdown(&app);
    return run_rc == 0 ? 0 : 1;
}
