#include "core/app.h"
#include "core/log.h"
#include "core/path.h"
#include "core/launch_options.h"
#include <stdio.h>

/* Entry point: initialise AppContext, run main loop, shut down.
 * All engine state lives on the stack-owned `app` (no globals).
 *
 * Returns: 0 on success, 1 on init failure.
 */
int main(int argc, char **argv)
{
    AppContext app;
    int width, height;
    int options = launch_options_parse(argc, argv, &width, &height);
    if (options != 0) {
        fprintf(options < 0 ? stderr : stdout, "Usage: TerraCraft [--window-size WIDTHxHEIGHT] [--help]\n");
        return options < 0 ? 1 : 0;
    }
    if (path_prepare_runtime_directory() != 0) {
        LOG_ERROR("Could not select runtime data directory");
        return 1;
    }

#ifdef NDEBUG
    /* Release ships quiet: per-chunk mesher chatter and other hot-path
     * DEBUG lines stay a debug-build tool, not a shipped log firehose. */
    log_set_level(LOG_LEVEL_INFO);
#endif

    if (app_init(&app, width, height, "TerraCraft") != 0) {
        LOG_ERROR("Failed to initialise TerraCraft. See logs above.");
        return 1;
    }

    int run_rc = app_run(&app);
    app_shutdown(&app);
    return run_rc == 0 ? 0 : 1;
}
