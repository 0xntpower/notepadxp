#include "app/AlertBoxPrompts.hpp"

#include "Resource.h"
#include "util/StringTable.hpp"

namespace notepadxp::app {

file::FilePrompts::SaveChoice AlertBoxPrompts::AskSaveChanges(const std::wstring& documentName) {
    const std::wstring text = util::LoadAndMerge(IDS_SCBC, documentName);
    const int answer =
        util::AlertBox(owner_, util::LoadStr(IDS_NN), text, MB_YESNOCANCEL | MB_ICONEXCLAMATION);
    if (answer == IDYES) {
        return SaveChoice::Save;
    }
    if (answer == IDCANCEL) {
        return SaveChoice::Cancel;
    }
    return SaveChoice::Discard;
}

bool AlertBoxPrompts::AskCreateNewFile(const std::wstring& fileName) {
    const std::wstring message = util::LoadAndMerge(IDS_FNF, fileName);
    return util::AlertBox(owner_, util::LoadStr(IDS_NN), message,
                          MB_YESNO | MB_ICONEXCLAMATION) == IDYES;
}

bool AlertBoxPrompts::AskContinueLossySave(const std::wstring& fileName) {
    const std::wstring message = util::LoadAndMerge(IDS_ERRUNICODE, fileName);
    return util::AlertBox(owner_, util::LoadStr(IDS_NN), message,
                          MB_OKCANCEL | MB_ICONEXCLAMATION) != IDCANCEL;
}

void AlertBoxPrompts::ReportError(Error error, const std::wstring& fileName) {
    UINT stringId = IDS_DISKERROR;
    switch (error) {
        case Error::OpenFailed:
        case Error::WriteFailed:
            stringId = IDS_DISKERROR;
            break;
        case Error::TooLarge:
            stringId = IDS_FTL;
            break;
        case Error::CreateFailed:
            stringId = IDS_CREATEERR;
            break;
    }
    util::AlertBox(owner_, util::LoadStr(IDS_NN), util::LoadAndMerge(stringId, fileName),
                   MB_OK | MB_ICONEXCLAMATION);
}

} // namespace notepadxp::app
