#pragma once

#include "Archive/Archive.h"
#include "General/UndoRedo.h"
#include "Graphics/CTexture/PatchTable.h"
#include "PatchBrowser.h"
#include "PatchTablePanel.h"
#include "TextureEditorPanel.h"
#include "TextureXPanel.h"
#include "UI/Controls/STabCtrl.h"

class TextureXEditor : public wxPanel, public Listener
{
public:
	TextureXEditor(wxWindow* parent);
	~TextureXEditor();

	Archive*     archive() const { return archive_; }
	PatchTable&  patchTable() { return patch_table_; }
	void         pnamesModified(bool mod = true) { pnames_modified_ = mod; }
	UndoManager* undoManager() const { return undo_manager_.get(); }

	bool openArchive(Archive* archive);
	void updateTexturePalette();
	void saveChanges();
	bool close();
	void showTextureMenu(bool show = true) const;
	void setSelection(size_t index) const;
	void setSelection(ArchiveEntry* entry) const;
	void updateMenuStatus();
	void undo();
	void redo();

	// Editing
	void     setFullPath(bool enabled = false) const { patch_browser_->setFullPath(enabled); }
	bool     removePatch(unsigned index, bool delete_entry = false);
	int      browsePatchTable(const wxString& first = "") const;
	wxString browsePatchEntry(const wxString& first = "");

	// Checks
	bool checkTextures();

	// Static
	static bool setupTextureEntries(Archive* archive);

private:
	Archive*                     archive_ = nullptr;       // The archive this editor is handling
	ArchiveEntry*                pnames_  = nullptr;       // The PNAMES entry to modify (can be null)
	PatchTable                   patch_table_;             // The patch table for TEXTURE1/2 (ie PNAMES)
	vector<TextureXPanel*>       texture_editors_;         // One panel per TEXTUREX list (ie TEXTURE1/TEXTURE2)
	PatchBrowser*                patch_browser_ = nullptr; // The patch browser window
	std::unique_ptr<UndoManager> undo_manager_  = nullptr;

	// UI Stuff
	TabControl* tabs_         = nullptr;
	wxMenu*     menu_texture_ = nullptr;

	bool pb_update_       = true;
	bool pnames_modified_ = false;

	// Events
	void onAnnouncement(Announcer* announcer, std::string_view event_name, MemChunk& event_data) override;
	void onShow(wxShowEvent& e);
};
