#include "full_screen.hpp"

#include <windows.h>
#include <shellapi.h>

namespace outatime {

// "Games" is the same check as PowerToys' game mode detection. Like PowerToys,
// this trusts the shell rather than checking whether a window covers the
// monitor: that would also pause in full-screen browsers and editors, where
// switching the IME is still wanted.
bool IsFullScreenAppInFront(FullScreenPause setting) {
    if (setting == FullScreenPause::Off) {
        return false;
    }
    QUERY_USER_NOTIFICATION_STATE state;
    if (FAILED(SHQueryUserNotificationState(&state))) {
        return false;
    }
    return state == QUNS_RUNNING_D3D_FULL_SCREEN ||
           (setting == FullScreenPause::All && state == QUNS_BUSY);
}

}  // namespace outatime
