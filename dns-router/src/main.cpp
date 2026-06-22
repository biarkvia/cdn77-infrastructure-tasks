#include "Router.h"

#include <iostream>
#include <string>

namespace {

void loadExampleRoutes(Router & router) {
    router.insert("2001:49f0:d0b8::/48", 174);
    router.insert("2402:8100:2582::/48", 215);
    router.insert("240e:438:1e30::/44", 103);
    router.insert("2804:1c1c:3000::/36", 198);
    router.insert("2804:1c1c:3000::/48", 199);
}

bool expectRoute(const Router & router, const std::string & ecs, uint16_t expected_pop, int expected_scope) {
    const RouteResult result = router.route(ecs);

    if (!result.found || result.pop != expected_pop || result.scope != expected_scope) {
        std::cerr << "route(" << ecs << ") failed: " << "found=" << result.found << " pop=" << result.pop << " scope=" << result.scope << '\n';
        return false;
    }

    return true;
}

bool expectMiss(const Router & router, const std::string & ecs) {
    const RouteResult result = router.route(ecs);

    if (result.found) {
        std::cerr << "route(" << ecs << ") should miss, got pop=" << result.pop << " scope=" << result.scope << '\n';
        return false;
    }

    return true;
}

} // namespace

int main() {
    Router router;
    loadExampleRoutes(router);

    bool ok = true;

    ok = expectRoute(router, "2402:8100:2582::/48", 215, 48) && ok;
    ok = expectRoute(router, "2001:49f0:d0b8:8a00::/56", 174, 48) && ok;
    ok = expectRoute(router, "2804:1c1c:3000:1::/64", 199, 48) && ok;
    ok = expectMiss(router, "2001:db8::/32") && ok;

    if (!ok) {
        return 1;
    }

    std::cout << "CDN DNS router self-check OK\n";
    return 0;
}
