# New app checks

Run `./tests/check-new-apps.sh` for the pure GitHub parser and ISS geometry/history tests.
They use the C compiler, SDK sources, local mocks and address/undefined-behaviour sanitizers.
No network is needed. Run `../perinkpheral/tools/perinkpheral sim-test tests` for the panel
scenarios, including the English and Russian versions of status, github and iss.

The initial pictures for these apps were inspected using the simulator's actual WASM
interpreter, services, event delivery and drawing code under a temporary offline driver.
The sandbox refused the normal runner's listening socket. The offline driver advanced the
same deadlines and consumed the same local mock responses through the simulator's job pipe.
These pictures still need confirmation with the normal `sim-test tests` command in an
environment that permits localhost sockets; no normal simulator pass is claimed here.
