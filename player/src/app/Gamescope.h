#pragma once

class QWindow;

namespace casdap {

void claimGamescopeFocus(QWindow *window);
bool setOverlayAtoms(QWindow *window);
bool setToastAtoms(QWindow *window);

}
