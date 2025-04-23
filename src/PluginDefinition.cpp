//this file is part of notepad++
//Copyright (C)2022 Don HO <don.h@free.fr>
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
//
//This program is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include "PluginDefinition.h"
#include "menuCmdID.h"
#include "DockingFeature/resource.h"
#include <windows.h>
#include <vector>
#include <sstream>
#include <string>
#include <cstdlib>
#include <cwchar>
#include <algorithm>
#include <cstdio>
#include <sstream>
#include <shlobj.h>
#include "CommitTree.h"
#include <commctrl.h>
#include <stdexcept>


/*
* TODO: 
* Fix all 'X' buttons and scrolling between old commits
*/

// GLOBALS

HINSTANCE g_hInst = NULL;
std::wstring g_repoPath = L"F:\\CSI5610\\Repo";
std::shared_ptr<CommitNode> g_commitTree = nullptr;
int g_commitCounter = 1;
static wchar_t g_commitMsgBuffer[512] = { 0 };
HWND g_hFileListDlg = NULL;


struct TimelineData {
    std::vector<CommitInfo> commits;
    std::wstring folderPath;
};


struct ViewCommitContext {
    int currentCommit;           // The commit number currently displayed.
    std::wstring repoPath;       // The repository folder path.
};


// Function Declerations
void InitializeCommitTree(const std::wstring& repoFolder);
INT_PTR CALLBACK ViewOnlyDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
void viewCommitInReadOnlyDialog(const std::wstring& fullPath);
std::wstring computeDiffSummary(const std::string& oldText, const std::string& newText);
std::wstring promptForCommitMessage();
static INT_PTR CALLBACK CommitMessageDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
std::wstring LoadRepoPath();
void SaveRepoPath(const std::wstring& newPath);


//
// The plugin data that Notepad++ needs
//
FuncItem funcItem[nbFunc];

//
// The data of Notepad++ that you can use in your plugin commands
//
NppData nppData;

//
// Initialize your plugin data here
// It will be called while plugin loading   
void pluginInit(HANDLE hModule)
{
    g_hInst = reinterpret_cast<HINSTANCE>(hModule);
    g_repoPath = LoadRepoPath();
    InitializeCommitTree(g_repoPath);
}

//
// Here you can do the clean up, save the parameters (if any) for the next session
//
void pluginCleanUp()
{
}

//
// Initialization of your plugin commands
// You should fill your plugins commands here
void commandMenuInit()
{

    //--------------------------------------------//
    //-- STEP 3. CUSTOMIZE YOUR PLUGIN COMMANDS --//
    //--------------------------------------------//
    // with function :
    // setCommand(int index,                      // zero based number to indicate the order of command
    //            TCHAR *commandName,             // the command name that you want to see in plugin menu
    //            PFUNCPLUGINCMD functionPointer, // the symbol of function (function pointer) associated with this command. The body should be defined below. See Step 4.
    //            ShortcutKey *shortcut,          // optional. Define a shortcut to trigger this command
    //            bool check0nInit                // optional. Make this menu item be checked visually
    //            );
    setCommand(0, TEXT("Open Versioned File"), openVersionedFile, NULL, false);
    setCommand(1, TEXT("Set Repo Location"), setRepoLocation, NULL, false);
    setCommand(2, TEXT("Commit Current File"), commitCurrentFile, NULL, false);
}

//
// Here you can do the clean up (especially for the shortcut)
//
void commandMenuCleanUp()
{
    // Don't forget to deallocate your shortcut here
}


//
// This function help you to initialize your plugin commands
//
bool setCommand(size_t index, TCHAR* cmdName, PFUNCPLUGINCMD pFunc, ShortcutKey* sk, bool check0nInit)
{
    if (index >= nbFunc)
        return false;

    if (!pFunc)
        return false;

    lstrcpy(funcItem[index]._itemName, cmdName);
    funcItem[index]._pFunc = pFunc;
    funcItem[index]._init2Check = check0nInit;
    funcItem[index]._pShKey = sk;

    return true;
}

//----------------------------------------------//
//-- STEP 4. DEFINE YOUR ASSOCIATED FUNCTIONS --//
//----------------------------------------------//
std::vector<std::wstring> GetTextFiles(const std::wstring& folderPath)
{
    std::vector<std::wstring> files;
    WIN32_FIND_DATA findFileData;
    std::wstring searchPath = folderPath + L"\\*.txt";
    HANDLE hFind = FindFirstFile(searchPath.c_str(), &findFileData);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            // Skip directories
            if (!(findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            {
                files.push_back(findFileData.cFileName);
            }
        } while (FindNextFile(hFind, &findFileData));
        FindClose(hFind);
    }
    return files;
}

std::string ReadFileAsString(const std::wstring& filePath)
{
    FILE* fp = _wfopen(filePath.c_str(), L"rb");
    if (!fp)
        return "";

    std::ostringstream oss;
    char buffer[1024];
    size_t bytesRead;
    while ((bytesRead = fread(buffer, 1, sizeof(buffer), fp)) > 0)
    {
        oss.write(buffer, bytesRead);
    }
    fclose(fp);
    return oss.str();
}

INT_PTR CALLBACK FileListDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    static std::vector<std::wstring>* pFiles = nullptr;
    static std::wstring folderPath;

    switch (message)
    {
        case WM_INITDIALOG:
        {
            auto* params = reinterpret_cast<std::pair<std::vector<std::wstring>*, std::wstring>*>(lParam);
            pFiles = params->first;
            folderPath = params->second;

            HWND hList = GetDlgItem(hDlg, IDC_FILE_LIST);

            // Populate list box
            for (const auto& file : *pFiles)
            {
                SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)file.c_str());
            }
            return TRUE;
        }
        case WM_COMMAND:
        {
            if (LOWORD(wParam) == IDOK)
            {
                HWND hList = GetDlgItem(hDlg, IDC_FILE_LIST);
                int sel = (int)SendMessage(hList, LB_GETCURSEL, 0, 0);
                if (sel != LB_ERR)
                {
                    wchar_t fileName[260];
                    SendMessage(hList, LB_GETTEXT, sel, (LPARAM)fileName);

                    // Build the full file path:
                    std::wstring fullPath = folderPath + L"\\" + fileName;

                    // Read the contents of the selected file:
                    std::string fileContents = ReadFileAsString(fullPath);

                    // Get the current Scintilla editor:
                    int which = -1;
                    ::SendMessage(nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&which);
                    if (which != -1)
                    {
                        HWND curScintilla = (which == 0) ? nppData._scintillaMainHandle : nppData._scintillaSecondHandle;

                        // Overwrite the current document with the file contents:
                        ::SendMessage(curScintilla, SCI_SETTEXT, 0, (LPARAM)fileContents.c_str());
                    }
                }
                EndDialog(hDlg, IDOK);
            }
            else if (LOWORD(wParam) == IDCANCEL)
            {
                EndDialog(hDlg, IDCANCEL);
            }
            return TRUE;
        }
    }

    return FALSE;
}


INT_PTR CALLBACK TimelineDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    static TimelineData* pData = nullptr;
    switch (message)
    {
    case WM_INITDIALOG:
    {
        g_hFileListDlg = hDlg;
        pData = reinterpret_cast<TimelineData*>(lParam);
        HWND hList = GetDlgItem(hDlg, IDC_FILE_LIST);
        // Enable full row selection.
        ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT);

        // Column 0: Commit Number
        LVCOLUMN lvCol = { 0 };
        lvCol.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        lvCol.pszText = const_cast<LPWSTR>(L"Commit");
        lvCol.cx = 50;
        ListView_InsertColumn(hList, 0, &lvCol);

        // Column 1: Filename
        lvCol.pszText = const_cast<LPWSTR>(L"Filename");
        lvCol.cx = 150;
        ListView_InsertColumn(hList, 1, &lvCol);

        // Column 2: Diff summary
        lvCol.pszText = const_cast<LPWSTR>(L"Diff");
        lvCol.cx = 200;
        ListView_InsertColumn(hList, 2, &lvCol);

        // Column 3: Commit Message
        lvCol.pszText = const_cast<LPWSTR>(L"Message");
        lvCol.cx = 200;
        ListView_InsertColumn(hList, 3, &lvCol);

        // Populate the list view.
        for (size_t i = 0; i < pData->commits.size(); i++) {
            LVITEM lvItem = { 0 };
            lvItem.mask = LVIF_TEXT;
            lvItem.iItem = (int)i;
            // Column 0: commit number
            std::wstring commitStr = std::to_wstring(pData->commits[i].commitNumber);
            lvItem.pszText = const_cast<LPWSTR>(commitStr.c_str());
            ListView_InsertItem(hList, &lvItem);

            // Column 1: filename
            ListView_SetItemText(hList, (int)i, 1, const_cast<LPWSTR>(pData->commits[i].fileName.c_str()));

            // Column 2: diff summary
            ListView_SetItemText(hList, (int)i, 2, const_cast<LPWSTR>(pData->commits[i].diffData.c_str()));

            // Column 3: commit message
            ListView_SetItemText(hList, (int)i, 3, const_cast<LPWSTR>(pData->commits[i].commitMessage.c_str()));
        }

    return TRUE;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK)
        {
            HWND hList = GetDlgItem(hDlg, IDC_FILE_LIST);
            int sel = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (sel != -1)
            {
                // Get the commit details
                auto commitPair = pData->commits[sel];
                std::wstring commitFileName = commitPair.fileName;
                std::wstring fullPath = pData->folderPath + L"\\" + commitFileName;

                // Check if this is the newest commit:
                if (commitPair.commitNumber == g_commitCounter - 1)
                {
                    // Load the newest commit directly into Notepad++
                    std::string fileContents = ReadFileAsString(fullPath);
                    int which = -1;
                    ::SendMessage(nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&which);
                    if (which != -1)
                    {
                        HWND curScintilla = (which == 0)
                            ? nppData._scintillaMainHandle
                            : nppData._scintillaSecondHandle;
                        ::SendMessage(curScintilla, SCI_SETTEXT, 0, (LPARAM)fileContents.c_str());
                    }
                    // For the newest commit, close the file list dialog.
                    EndDialog(hDlg, IDOK);
                }
                else
                {
                    // For an older commit, open it in the view-only dialog.
                    viewCommitInReadOnlyDialog(fullPath);
                }
            }
            return TRUE;
        }
        else if (LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}


// dialog procedure for view-only commits mode.
INT_PTR CALLBACK ViewOnlyDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_INITDIALOG) {
        SetWindowLongPtr(hDlg, GWLP_USERDATA, lParam);
        ViewCommitContext* pContext = reinterpret_cast<ViewCommitContext*>(lParam);
        // Load and display the current commit file.
        std::wstring commitFileName = L"commit_" + std::to_wstring(pContext->currentCommit) + L".txt";
        std::wstring fullPath = pContext->repoPath + L"\\" + commitFileName;
        std::string fileContents = ReadFileAsString(fullPath);

        // Convert UTF-8 file content to wide string.
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, fileContents.c_str(), -1, NULL, 0);
        std::wstring wcontent(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, fileContents.c_str(), -1, &wcontent[0], size_needed);

        HWND hEdit = GetDlgItem(hDlg, IDC_VIEW_EDIT);
        SetWindowText(hEdit, wcontent.c_str());
        return TRUE;
    }

    if (message == WM_COMMAND) {
        // Retrieve context pointer.
        ViewCommitContext* pContext = reinterpret_cast<ViewCommitContext*>(GetWindowLongPtr(hDlg, GWLP_USERDATA));

        switch (LOWORD(wParam)) {
        case IDC_PREV:
        {
            auto pred = getPredecessor(g_commitTree, pContext->currentCommit, g_commitCounter - 1);
            if (pred) {
                pContext->currentCommit = pred->commitCounter;
                std::wstring commitFileName = L"commit_" + std::to_wstring(pContext->currentCommit) + L".txt";
                std::wstring fullPath = pContext->repoPath + L"\\" + commitFileName;
                std::string fileContents = ReadFileAsString(fullPath);
                int size_needed = MultiByteToWideChar(CP_UTF8, 0, fileContents.c_str(), -1, NULL, 0);
                std::wstring wcontent(size_needed, 0);
                MultiByteToWideChar(CP_UTF8, 0, fileContents.c_str(), -1, &wcontent[0], size_needed);
                HWND hEdit = GetDlgItem(hDlg, IDC_VIEW_EDIT);
                SetWindowText(hEdit, wcontent.c_str());
            }
            return TRUE;
        }
        case IDC_NEXT:
        {
            auto succ = getSuccessor(g_commitTree, pContext->currentCommit, g_commitCounter - 1);
            if (succ) {
                pContext->currentCommit = succ->commitCounter;
                std::wstring commitFileName = L"commit_" + std::to_wstring(pContext->currentCommit) + L".txt";
                std::wstring fullPath = pContext->repoPath + L"\\" + commitFileName;
                std::string fileContents = ReadFileAsString(fullPath);
                int size_needed = MultiByteToWideChar(CP_UTF8, 0, fileContents.c_str(), -1, NULL, 0);
                std::wstring wcontent(size_needed, 0);
                MultiByteToWideChar(CP_UTF8, 0, fileContents.c_str(), -1, &wcontent[0], size_needed);
                HWND hEdit = GetDlgItem(hDlg, IDC_VIEW_EDIT);
                SetWindowText(hEdit, wcontent.c_str());
            }
            return TRUE;
        }
        case IDC_ROLLBACK:
        {
            // Confirm rollback with the user.
            int confirm = MessageBox(hDlg,
                L"Are you sure you want to rollback? This will permanently remove all commits newer than the currently viewed commit.",
                L"Confirm Rollback", MB_YESNO | MB_ICONWARNING);
            if (confirm == IDYES) {
                int rollbackCommit = pContext->currentCommit;

                // Delete all commit files with commit numbers greater than the currently viewed commit.
                for (int i = rollbackCommit + 1; i < g_commitCounter; i++) {
                    std::wstring commitFileName = L"commit_" + std::to_wstring(i) + L".txt";
                    std::wstring fullPath = g_repoPath + L"\\" + commitFileName;
                    _wremove(fullPath.c_str());

                    std::wstring diffFileName = L"commit_" + std::to_wstring(i) + L".diff";
                    std::wstring diffFullPath = g_repoPath + L"\\" + diffFileName;
                    _wremove(diffFullPath.c_str());

                    std::wstring msgFileName = L"commit_" + std::to_wstring(i) + L".msg";
                    std::wstring msgFullPath = g_repoPath + L"\\" + msgFileName;
                    _wremove(msgFullPath.c_str());
                }

                // Update the commit counter so that it is one more than the rollback commit.
                g_commitCounter = rollbackCommit + 1;

                // Reinitialize the commit tree from the repository (only the remaining commits will be loaded).
                InitializeCommitTree(g_repoPath);

                // Load the rollback commit into Notepad++.
                int which = -1;
                ::SendMessage(nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&which);
                if (which != -1) {
                    HWND curScintilla = (which == 0) ? nppData._scintillaMainHandle : nppData._scintillaSecondHandle;
                    std::wstring commitFileName = L"commit_" + std::to_wstring(rollbackCommit) + L".txt";
                    std::wstring fullPath = g_repoPath + L"\\" + commitFileName;
                    std::string fileContents = ReadFileAsString(fullPath);
                    ::SendMessage(curScintilla, SCI_SETTEXT, 0, (LPARAM)fileContents.c_str());
                }

                if (g_hFileListDlg != NULL) {
                    EndDialog(g_hFileListDlg, IDC_ROLLBACK);
                    g_hFileListDlg = NULL;
                }

                MessageBox(hDlg, L"Rollback successful.", L"Rollback", MB_OK);
                EndDialog(hDlg, IDC_ROLLBACK);
            }
            return TRUE;
        }
        case IDOK:
        case IDCANCEL:
        {
            EndDialog(hDlg, LOWORD(wParam));
            delete pContext;
            return TRUE;
        }
        }
    }

    if (message == WM_CLOSE) {
        ViewCommitContext* pContext = reinterpret_cast<ViewCommitContext*>(GetWindowLongPtr(hDlg, GWLP_USERDATA));
        EndDialog(hDlg, IDCANCEL);
        delete pContext;
        return TRUE;
    }
    return FALSE;
}


// Function that handles view commits window
void viewCommitInReadOnlyDialog(const std::wstring& fullPath)
{
    size_t pos1 = fullPath.find(L"commit_");
    if (pos1 == std::wstring::npos) return;
    size_t pos2 = fullPath.find(L".txt", pos1);
    if (pos2 == std::wstring::npos) return;
    std::wstring numberStr = fullPath.substr(pos1 + 7, pos2 - (pos1 + 7));
    int commitNum = _wtoi(numberStr.c_str());

    // Allocate and initialize the context.
    ViewCommitContext* pContext = new ViewCommitContext;
    pContext->currentCommit = commitNum;
    pContext->repoPath = g_repoPath;

    DialogBoxParam(
        g_hInst,
        MAKEINTRESOURCE(IDD_VIEW_ONLY_DLG),
        nppData._nppHandle,
        ViewOnlyDlgProc,
        reinterpret_cast<LPARAM>(pContext)
    );
}


// Lists out all commits in a summary view
void openVersionedFile()
{
    // If no commits exist, notify the user.
    if (!g_commitTree)
    {
        ::MessageBox(NULL, TEXT("No commits available."), TEXT("Info"), MB_OK);
        return;
    }

    // Build a vector of commit pairs (commit number and filename) by in-order traversal.
    std::vector<CommitInfo> commitList;
    for (int i = 1; i < g_commitCounter; i++) {
        auto node = searchCommit(g_commitTree, i, g_commitCounter - 1);
        if (node) {
            commitList.push_back({ node->commitCounter, node->fileName, node->diffData, node->commitMessage });
        }
    }
    if (commitList.empty())
    {
        ::MessageBox(NULL, TEXT("No commits available."), TEXT("Info"), MB_OK);
        return;
    }

    // Prepare timeline data to pass to the dialog.
    TimelineData timelineData;
    timelineData.commits = commitList;
    timelineData.folderPath = g_repoPath;

    // Display the dialog
    DialogBoxParam(
        g_hInst,
        MAKEINTRESOURCE(IDD_FILE_LIST_DLG),
        nppData._nppHandle,
        TimelineDlgProc,
        (LPARAM)&timelineData);
}


// Helper function for setting up repo
std::wstring BrowseForFolder(HWND owner, const wchar_t* title)
{
    std::wstring folder;
    BROWSEINFO bi = { 0 };
    bi.hwndOwner = owner;
    bi.lpszTitle = title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolder(&bi);
    if (pidl)
    {
        wchar_t path[MAX_PATH];
        if (SHGetPathFromIDList(pidl, path))
        {
            folder = path;
        }
        CoTaskMemFree(pidl);
    }
    return folder;
}


// Setting the repo location
void setRepoLocation()
{
    std::wstring chosenFolder = BrowseForFolder(nppData._nppHandle, L"Select Repository Folder");
    if (!chosenFolder.empty())
    {
        g_repoPath = chosenFolder;
        SaveRepoPath(chosenFolder);
        g_commitTree = nullptr;
        InitializeCommitTree(g_repoPath);
        std::wstring msg = L"Repository location set to:\n" + chosenFolder;
        ::MessageBox(NULL, msg.c_str(), L"Repository Location", MB_OK);
    }
    else
    {
        ::MessageBox(NULL, L"No folder was selected.", L"Repository Location", MB_OK);
    }
}


// Commiting a file
void commitCurrentFile() {
    // Get the current Scintilla editor handle
    int which = -1;
    ::SendMessage(nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&which);
    if (which == -1)
    {
        ::MessageBox(NULL, TEXT("No active document found."), TEXT("Commit Error"), MB_OK);
        return;
    }
    HWND curScintilla = (which == 0) ? nppData._scintillaMainHandle : nppData._scintillaSecondHandle;

    int textLength = (int)::SendMessage(curScintilla, SCI_GETLENGTH, 0, 0);
    char* textBuffer = new char[textLength + 1];
    ::SendMessage(curScintilla, SCI_GETTEXT, textLength + 1, (LPARAM)textBuffer);
    std::string currentFileText(textBuffer, textLength);
    delete[] textBuffer;

    // calculate the file names for the new and previous commit.
    std::wstring commitFileName = L"commit_" + std::to_wstring(g_commitCounter) + L".txt";
    std::wstring fullPath = g_repoPath + L"\\" + commitFileName;

    // handle commit message
    std::wstring commitMessage = promptForCommitMessage();
    if (commitMessage.empty()) {
        ::MessageBox(NULL, TEXT("Commit cancelled: no message entered."), TEXT("Commit Error"), MB_OK);
        return;
    }

    // Very basic diff generation (Will eventually replace this with an actual diffing library)
    std::wstring diffSummary = L"";
    if (g_commitCounter > 1) {
        std::wstring prevCommitFileName = L"commit_" + std::to_wstring(g_commitCounter - 1) + L".txt";
        std::wstring prevFullPath = g_repoPath + L"\\" + prevCommitFileName;
        std::string prevFileText = ReadFileAsString(prevFullPath);
        diffSummary = computeDiffSummary(prevFileText, currentFileText);
    }

    FILE* fp = _wfopen(fullPath.c_str(), L"wb");
    if (!fp) {
        ::MessageBox(NULL, TEXT("Error writing commit file."), TEXT("Commit Error"), MB_OK);
        return;
    }
    fwrite(currentFileText.c_str(), sizeof(char), currentFileText.size(), fp);
    fclose(fp);

    // Create the diff file name (e.g., commit_3.diff) and full path.
    std::wstring diffFileName = L"commit_" + std::to_wstring(g_commitCounter) + L".diff";
    std::wstring diffFullPath = g_repoPath + L"\\" + diffFileName;

    // Convert diffSummary (std::wstring) to a UTF-8 std::string.
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, diffSummary.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string diffSummaryStr(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, diffSummary.c_str(), -1, &diffSummaryStr[0], size_needed, nullptr, nullptr);

    // Write the diff to the diff file.
    FILE* diff_fp = _wfopen(diffFullPath.c_str(), L"wb");
    if (diff_fp) {
        fwrite(diffSummaryStr.c_str(), sizeof(char), diffSummaryStr.size(), diff_fp);
        fclose(diff_fp);
    }
    else {
        ::MessageBox(NULL, TEXT("Error writing diff file."), TEXT("Commit Error"), MB_OK);
    }

    // Create a file name for the commit message, e.g., commit_3.msg:
    std::wstring msgFileName = L"commit_" + std::to_wstring(g_commitCounter) + L".msg";
    std::wstring msgFullPath = g_repoPath + L"\\" + msgFileName;
    FILE* msg_fp = _wfopen(msgFullPath.c_str(), L"wb");
    if (msg_fp) {
        // Convert the commit message (std::wstring) to UTF-8 std::string
        int msg_size_needed = WideCharToMultiByte(CP_UTF8, 0, commitMessage.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string commitMessageStr(msg_size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, commitMessage.c_str(), -1, &commitMessageStr[0], msg_size_needed, nullptr, nullptr);

        // Write the commit message (without the null terminator)
        fwrite(commitMessageStr.c_str(), sizeof(char), commitMessageStr.size() - 1, msg_fp);
        fclose(msg_fp);
    }
    else {
        ::MessageBox(NULL, TEXT("Error writing commit message file."), TEXT("Commit Error"), MB_OK);
    }

    // Insert the new commit into the persistent AVL tree
    g_commitTree = insertNode(g_commitTree, g_commitCounter, commitFileName, diffSummary, commitMessage);
    g_commitCounter++;


    std::wstring msg = L"File committed as " + commitFileName;
    ::MessageBox(NULL, msg.c_str(), L"Commit Successful", MB_OK);

}


// Basic function for generating a diff summary, will eventually replace this with actual diffing
std::wstring computeDiffSummary(const std::string& oldText, const std::string& newText) {
    std::istringstream oldStream(oldText);
    std::istringstream newStream(newText);
    std::string oldLine, newLine;
    int added = 0, removed = 0;

    // line-by-line comparison, this is temporary, needs future work
    while (std::getline(oldStream, oldLine) && std::getline(newStream, newLine)) {
        if (oldLine != newLine) {
            added++;
            removed++;
        }
    }
    // Count any remaining lines.
    while (std::getline(oldStream, oldLine)) removed++;
    while (std::getline(newStream, newLine)) added++;

    std::wstringstream wss;
    wss << L"Added: " << added << L", Removed: " << removed;
    return wss.str();
}


// Parse the repo folder and populate the commit tree for the current Notepad++ session
void InitializeCommitTree(const std::wstring& repoFolder)
{
    // Get all text files from the repo folder.
    std::vector<std::wstring> files = GetTextFiles(repoFolder);
    int maxCommit = 0;

    // Iterate through each file.
    for (const auto& file : files)
    {
        // Check if file name matches the pattern "commit_<number>.txt"
        std::wstring prefix = L"commit_";
        std::wstring suffix = L".txt";
        if (file.compare(0, prefix.size(), prefix) == 0 &&
            file.size() > prefix.size() + suffix.size() &&
            file.compare(file.size() - suffix.size(), suffix.size(), suffix) == 0)
        {
            std::wstring numStr = file.substr(prefix.size(), file.size() - prefix.size() - suffix.size());
            int commitNum = _wtoi(numStr.c_str());

            std::wstring diffFileName = L"commit_" + std::to_wstring(commitNum) + L".diff";
            std::wstring diffFullPath = repoFolder + L"\\" + diffFileName;
            std::string diffDataStr = ReadFileAsString(diffFullPath);
            std::wstring diffData(diffDataStr.begin(), diffDataStr.end());

            std::wstring msgFileName = L"commit_" + std::to_wstring(commitNum) + L".msg";
            std::wstring msgFullPath = repoFolder + L"\\" + msgFileName;
            std::string commitMsgStr = ReadFileAsString(msgFullPath);
            std::wstring commitMsg(commitMsgStr.begin(), commitMsgStr.end());

            // Insert into commit tree
            g_commitTree = insertNode(g_commitTree, commitNum, file, diffData, commitMsg);

            if (commitNum > maxCommit)
                maxCommit = commitNum;
        }
    }
    // Set the global commit counter to one more than the highest commit number.
    g_commitCounter = maxCommit + 1;
}


std::wstring promptForCommitMessage() {
    INT_PTR result = DialogBox(g_hInst, MAKEINTRESOURCE(IDD_COMMIT_MSG_DLG), nppData._nppHandle, CommitMessageDlgProc);
    if (result == 1) {
        return std::wstring(g_commitMsgBuffer);
    }
    return L""; // return an empty string if cancelled or error
}


static INT_PTR CALLBACK CommitMessageDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (message) {
    case WM_INITDIALOG:
        // Clear the buffer on init
        g_commitMsgBuffer[0] = L'\0';
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            // Retrieve text from the edit control.
            GetDlgItemText(hDlg, IDC_COMMIT_MSG_EDIT, g_commitMsgBuffer, 512);
            EndDialog(hDlg, 1);
            return TRUE;
        }
        else if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, 0);
            return TRUE;
        }
        break;
    }
    return FALSE;
}


// Use the user's AppData folder to store location for repo between Notepad++ Sessions
std::wstring GetConfigFilePath() {
    // Retrieve the path to the Local AppData folder.
    wchar_t appDataPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, appDataPath))) {
        std::wstring configDir = std::wstring(appDataPath) + L"\\Mini-VC";
        CreateDirectoryW(configDir.c_str(), NULL);

        return configDir + L"\\repo_path.txt";
    }
    // Fallback: if SHGetFolderPath fails, use a relative path.
    return L"repo_path.txt";
}


std::wstring LoadRepoPath() {
    std::wstring configFile = GetConfigFilePath();
    FILE* fp = _wfopen(configFile.c_str(), L"r");
    if (!fp) {
        // File does not exist; return a default repository path.
        return L"F:\\CSI5610\\Repo";
    }

    wchar_t pathBuffer[MAX_PATH] = { 0 };
    if (fgetws(pathBuffer, MAX_PATH, fp) == nullptr) {
        fclose(fp);
        return L"F:\\CSI5610\\Repo";
    }
    fclose(fp);

    // Remove any newline characters
    std::wstring repoPath(pathBuffer);
    repoPath.erase(std::remove(repoPath.begin(), repoPath.end(), L'\n'), repoPath.end());
    repoPath.erase(std::remove(repoPath.begin(), repoPath.end(), L'\r'), repoPath.end());
    return repoPath;
}


void SaveRepoPath(const std::wstring& newPath) {
    std::wstring configFile = GetConfigFilePath();
    FILE* fp = _wfopen(configFile.c_str(), L"w");
    if (fp) {
        fputws(newPath.c_str(), fp);
        fclose(fp);
    }
}