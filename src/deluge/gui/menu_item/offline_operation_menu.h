#pragma once

#include "gui/menu_item/menu_item.h"

namespace deluge::gui::menu_item {

class OfflineOperationMenu : public MenuItem {
public:
	OfflineOperationMenu();
	MenuItem* selectButtonPress() override;
	void selectEncoderAction(int32_t offset) override;
	void readValueAgain() override;
	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override;
	MenuPermission checkPermissionToBeginSession(ModControllableAudio* modControllable, int32_t whichThing,
	                                             ::MultiRange** currentRange) override;

private:
	void updateDisplay();
};

extern OfflineOperationMenu offlineOperationMenu;

} // namespace deluge::gui::menu_item
