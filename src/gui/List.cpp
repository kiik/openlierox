/*
 *  List.cpp
 *  OpenLieroX
 *
 *  Created by Albert Zeyer on 23.02.10.
 *  code under LGPL
 *
 */

#include "gui/List.h"
#include "Debug.h"

void GuiListItem::setImage(const SmartPointer<DynDrawIntf>&) {
	warnings << "GuiListItem::setImage not implemented" << endl;
}

typedef std::function< Iterator<GuiListItem::Pt>::Ref() > DynamicListFct;
static GuiList::Pt dynamicGuiList(DynamicListFct f) {
	struct DynamicList : GuiList {
		DynamicListFct f;
		DynamicList(DynamicListFct _f) : f(_f) {}
		virtual Iterator<GuiListItem::Pt>::Ref iterator() { return f(); }
	};
	return new DynamicList(f);
}

static Iterator<GuiListItem::Pt>::Ref iteratorForGuiList(const GuiItemList& l) {
	return GetIterator(l);
}

GuiList::Pt dynamicGuiList(std::function< GuiItemList() > f) {
	DynamicListFct fct = [f]() { return iteratorForGuiList(f()); };
	return dynamicGuiList(fct);
}
