#include "cbelt.h"
#include <windows.h>

#include "../../src/core/main_context.h"

#include "../../src/core/shared_state.h"

CBELT_GROUP("bfwm_lifecycle")

CBELT_TEST(init_initializes_registries) {
   SharedState shared = {};
   MainContext main = {};

   BFWMInit(&shared, &main);

   cbelt_assert_not_null(main.monitors);
   cbelt_assert_not_null(main.windows);
   cbelt_assert_not_null(main.dpi);
   cbelt_assert(shared.config.bar_cfg.enabled == TRUE);

   BFWMFree(&shared, &main);
   return TEST_SUCCESS;
}

CBELT_TEST(free_releases_registries) {
   SharedState shared = {};
   MainContext main = {};

   BFWMInit(&shared, &main);
   BFWMFree(&shared, &main);

   cbelt_assert(main.monitors == nullptr);
   cbelt_assert(main.windows == nullptr);
   cbelt_assert(main.dpi == nullptr);

   return TEST_SUCCESS;
}

CBELT_TEST(free_null_is_safe) {
   BFWMFree(nullptr, nullptr);
   return TEST_SUCCESS;
}
