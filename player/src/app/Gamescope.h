#pragma once

class QWindow;

namespace microdap {

void claimGamescopeFocus(QWindow *window);
bool setOverlayAtoms(QWindow *window);
bool setToastAtoms(QWindow *window);

}
