// ==WindhawkMod==
// @id              compact-start-menu-grid
// @name            Compact Start Menu Grid
// @description     Cleans up the Windows 11 Start menu by showing only a compact, ungrouped All apps view.
// @version         1.0.0
// @author          Asteski
// @github          https://github.com/Asteski
// @include         StartMenuExperienceHost.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -lruntimeobject
// ==/WindhawkMod==

// ==WindhawkModSettings==
/*
- hideTopLevelHeader: true
  $name: Hide top header
  $description: Hide the All apps top header row with the title and view selector.
- hideCategoryViewOption: true
  $name: Hide category view option
  $description: Hide the Category option from the All apps view selector.
- headerText: "All"
  $name: Change header text
  $description: Text to show in the All apps header.
- hiddenHeaderIconGap: 16
  $name: Hidden header icon gap
  $description: Top gap, in pixels, between search and the first icon row when the top header is hidden.
- hideScrollBar: false
  $name: Hide scroll bar
  $description: Hide the All apps visual scroll bar.
*/
// ==/WindhawkModSettings==

// ==WindhawkModReadme==
/*
# Compact Start Menu Grid
This mod simplifies the Start Menu by directly adjusting All apps XAML sections.

### Features:
* **Shows "All apps":** Keeps the app list visible.
* **Hides "Pinned":** Removes the section with pinned apps.
* **Hides "Recommended":** Removes the section with recent files.
* **Hides group headers:** Removes the visible All apps group headers in grid and list views.
* **Compacts grid view:** Reduces group-header spacing in the All apps grid.
* **Optional top header:** Can hide the All apps title/view row.
* **Optional category view hiding:** Can hide the Category option from the view selector.
* **Hidden header icon gap:** Can adjust the first icon row spacing when the top header is hidden.
* **Optional scroll bar hiding:** Can hide the visual All apps scroll bar.

### Instructions:
* Enable the mod in Windhawk.
* **Restart StartMenuExperienceHost.exe**, sign out/in, or restart your computer to apply changes.
*/
// ==/WindhawkModReadme==

#include <windhawk_utils.h>

#include <xamlom.h>

#include <atomic>
#include <string>
#include <vector>

#include <Unknwn.h>
#include <combaseapi.h>
#include <ocidl.h>

#undef GetCurrentTime

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Data.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/base.h>

namespace wf = winrt::Windows::Foundation;
namespace wfc = winrt::Windows::Foundation::Collections;
namespace wux = winrt::Windows::UI::Xaml;
namespace wuc = winrt::Windows::UI::Xaml::Controls;
namespace wucp = winrt::Windows::UI::Xaml::Controls::Primitives;
namespace wuxd = winrt::Windows::UI::Xaml::Data;

struct Settings {
    bool hideTopLevelHeader = true;
    std::wstring headerText = L"All";
    bool hideCategoryViewOption = true;
    int hiddenHeaderIconGap = 16;
    bool hideScrollBar = false;
};

Settings g_settings;

std::atomic<DWORD> g_targetThreadId = 0;

struct FlattenedSourceEntry {
    winrt::weak_ref<wuc::ItemsControl> itemsControl;
    wf::IInspectable originalSource{nullptr};
    wfc::IObservableVector<wf::IInspectable> flatSource{nullptr};
};

std::vector<FlattenedSourceEntry> g_flattenedSources;
std::vector<winrt::weak_ref<wuc::SemanticZoom>> g_hookedSemanticZooms;
std::vector<winrt::weak_ref<wuc::ItemsWrapGrid>> g_gapAdjustedItemsWrapGrids;

void LoadSettings() {
    g_settings.hideTopLevelHeader = Wh_GetIntSetting(L"hideTopLevelHeader") != 0;

    PCWSTR headerText = Wh_GetStringSetting(L"headerText");
    g_settings.headerText = headerText && *headerText ? headerText : L"All";
    Wh_FreeStringSetting(headerText);

    g_settings.hideCategoryViewOption =
        Wh_GetIntSetting(L"hideCategoryViewOption") != 0;
    g_settings.hiddenHeaderIconGap = Wh_GetIntSetting(L"hiddenHeaderIconGap");
    if (g_settings.hiddenHeaderIconGap < 0) {
        g_settings.hiddenHeaderIconGap = 0;
    }
    g_settings.hideScrollBar = Wh_GetIntSetting(L"hideScrollBar") != 0;
}

HMODULE GetCurrentModuleHandle() {
    HMODULE module;
    if (!GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (PCWSTR)GetCurrentModuleHandle, &module)) {
        return nullptr;
    }

    return module;
}

bool IsPinnedSectionElement(wux::FrameworkElement const& element, PCWSTR type) {
    auto name = element.Name();

    return name == L"PinnedListHeaderGrid" ||
           name == L"PinnedListHeaderText" ||
           name == L"ShowMorePinnedGrid" ||
           name == L"ShowMorePinnedButton" ||
           name == L"ShowMorePinnedButtonText" ||
           name == L"StartMenuPinnedList" ||
           name == L"PinnedList" ||
           name == L"PinnedListPipsPager" ||
           (type && lstrcmpiW(type, L"StartMenu.PinnedList") == 0);
}

bool IsGroupHeaderElement(wux::FrameworkElement const& element, PCWSTR type) {
    return element.try_as<wuc::GridViewHeaderItem>() ||
           element.try_as<wuc::ListViewHeaderItem>() ||
           (type && (lstrcmpiW(type, L"Windows.UI.Xaml.Controls.GridViewHeaderItem") == 0 ||
                     lstrcmpiW(type, L"Windows.UI.Xaml.Controls.ListViewHeaderItem") == 0));
}

void CollapseElement(wux::FrameworkElement const& element) {
    element.Visibility(wux::Visibility::Collapsed);
    element.Width(0);
    element.MinWidth(0);
    element.MaxWidth(0);
    element.MinHeight(0);
    element.Height(0);
    element.MaxHeight(0);
    element.Margin({});
    element.HorizontalAlignment(wux::HorizontalAlignment::Left);
    element.VerticalAlignment(wux::VerticalAlignment::Top);
    element.Opacity(0);
    element.IsHitTestVisible(false);
}

void KeepElementCollapsed(wux::FrameworkElement const& element) {
    element.RegisterPropertyChangedCallback(
        wux::UIElement::VisibilityProperty(),
        [](wux::DependencyObject const& sender, wux::DependencyProperty const&) {
            if (auto element = sender.try_as<wux::FrameworkElement>()) {
                if (element.Visibility() != wux::Visibility::Collapsed) {
                    CollapseElement(element);
                }
            }
        });
    element.RegisterPropertyChangedCallback(
        wux::FrameworkElement::WidthProperty(),
        [](wux::DependencyObject const& sender, wux::DependencyProperty const&) {
            if (auto element = sender.try_as<wux::FrameworkElement>()) {
                if (element.Width() != 0) {
                    CollapseElement(element);
                }
            }
        });
    element.RegisterPropertyChangedCallback(
        wux::FrameworkElement::HeightProperty(),
        [](wux::DependencyObject const& sender, wux::DependencyProperty const&) {
            if (auto element = sender.try_as<wux::FrameworkElement>()) {
                if (element.Height() != 0) {
                    CollapseElement(element);
                }
            }
        });
}

bool IsCategoryText(winrt::hstring const& text) {
    return lstrcmpiW(text.c_str(), L"Category") == 0;
}

wux::FrameworkElement FindCategoryViewOptionTarget(
    wux::FrameworkElement const& element) {
    if (auto menuFlyoutItem = element.try_as<wuc::MenuFlyoutItem>()) {
        if (IsCategoryText(menuFlyoutItem.Text())) {
            return element;
        }
    }

    if (auto textBlock = element.try_as<wuc::TextBlock>()) {
        if (!IsCategoryText(textBlock.Text())) {
            return nullptr;
        }

        auto current = element;
        while (current) {
            if (current.try_as<wuc::MenuFlyoutItem>()) {
                return current;
            }

            current = wux::Media::VisualTreeHelper::GetParent(current)
                          .try_as<wux::FrameworkElement>();
        }

        return element;
    }

    return nullptr;
}

void ApplyHeaderText(wux::FrameworkElement const& element) {
    if (element.Name() != L"AllListHeadingText") {
        return;
    }

    if (auto textBlock = element.try_as<wuc::TextBlock>()) {
        textBlock.Text(g_settings.headerText);
        textBlock.RegisterPropertyChangedCallback(
            wuc::TextBlock::TextProperty(),
            [](wux::DependencyObject const& sender, wux::DependencyProperty const&) {
                if (auto textBlock = sender.try_as<wuc::TextBlock>()) {
                    if (textBlock.Text() != g_settings.headerText) {
                        textBlock.Text(g_settings.headerText);
                    }
                }
            });
    }
}

void ApplyHiddenHeaderIconGap(wuc::ItemsWrapGrid const& itemsWrapGrid) {
    if (!g_settings.hideTopLevelHeader) {
        return;
    }

    auto margin = itemsWrapGrid.Margin();
    double topGap = static_cast<double>(g_settings.hiddenHeaderIconGap);
    if (margin.Top != topGap) {
        margin.Top = topGap;
        itemsWrapGrid.Margin(margin);
    }

    for (auto const& adjustedItemsWrapGrid : g_gapAdjustedItemsWrapGrids) {
        if (adjustedItemsWrapGrid.get() == itemsWrapGrid) {
            return;
        }
    }

    g_gapAdjustedItemsWrapGrids.push_back(winrt::make_weak(itemsWrapGrid));

    itemsWrapGrid.RegisterPropertyChangedCallback(
        wux::FrameworkElement::MarginProperty(),
        [](wux::DependencyObject const& sender, wux::DependencyProperty const&) {
            if (auto itemsWrapGrid = sender.try_as<wuc::ItemsWrapGrid>()) {
                ApplyHiddenHeaderIconGap(itemsWrapGrid);
            }
        });
}

bool IsInAllAppsArea(wux::FrameworkElement const& element) {
    for (auto current = element; current;) {
        if (current.Name() == L"AllAppsGrid" ||
            current.Name() == L"AppsList" ||
            (winrt::get_class_name(current) == L"StartDocked.AllAppsGridListView")) {
            return true;
        }

        current = wux::Media::VisualTreeHelper::GetParent(current)
                      .try_as<wux::FrameworkElement>();
    }

    return false;
}

void CompactAllAppsItemsWrapGrid(wuc::ItemsWrapGrid const& itemsWrapGrid) {
    if (!IsInAllAppsArea(itemsWrapGrid)) {
        return;
    }

    itemsWrapGrid.GroupPadding({});
    itemsWrapGrid.AreStickyGroupHeadersEnabled(false);
    itemsWrapGrid.GroupHeaderPlacement(wucp::GroupHeaderPlacement::Top);
    ApplyHiddenHeaderIconGap(itemsWrapGrid);
}

bool IsAllAppsGridView(wux::FrameworkElement const& element, PCWSTR type) {
    return element.Name() == L"AllAppsGrid" ||
           element.Name() == L"AppsList" ||
           (type && lstrcmpiW(type, L"StartDocked.AllAppsGridListView") == 0) ||
           winrt::get_class_name(element) == L"StartDocked.AllAppsGridListView";
}

void FlattenAllAppsGridSource(wux::FrameworkElement const& element,
                              PCWSTR type) {
    if (!IsAllAppsGridView(element, type)) {
        return;
    }

    auto itemsControl = element.try_as<wuc::ItemsControl>();
    if (!itemsControl) {
        return;
    }

    for (auto const& entry : g_flattenedSources) {
        if (entry.itemsControl.get() == itemsControl) {
            return;
        }
    }

    auto originalSource = itemsControl.ItemsSource();
    auto collectionView = originalSource.try_as<wuxd::ICollectionView>();
    if (!collectionView) {
        return;
    }

    auto groups = collectionView.CollectionGroups();
    if (!groups || groups.Size() == 0) {
        return;
    }

    std::vector<wf::IInspectable> flatItems;
    for (uint32_t groupIndex = 0; groupIndex < groups.Size(); groupIndex++) {
        auto group = groups.GetAt(groupIndex).try_as<wuxd::ICollectionViewGroup>();
        if (!group) {
            continue;
        }

        auto groupItems = group.GroupItems();
        if (!groupItems) {
            continue;
        }

        flatItems.reserve(flatItems.size() + groupItems.Size());
        for (uint32_t itemIndex = 0; itemIndex < groupItems.Size(); itemIndex++) {
            flatItems.push_back(groupItems.GetAt(itemIndex));
        }
    }

    if (flatItems.empty()) {
        return;
    }

    auto flatSource = winrt::single_threaded_observable_vector(std::move(flatItems));
    itemsControl.ItemsSource(flatSource);
    itemsControl.GroupStyle().Clear();

    g_flattenedSources.push_back(
        {winrt::make_weak(itemsControl), originalSource, flatSource});
}

void FlattenAllAppsGridSourceFromAncestor(wux::FrameworkElement const& element) {
    auto current = element;
    while (current) {
        FlattenAllAppsGridSource(current, nullptr);
        current = wux::Media::VisualTreeHelper::GetParent(current)
                      .try_as<wux::FrameworkElement>();
    }
}

void DisableCategorySemanticZoom(wuc::SemanticZoom const& semanticZoom) {
    if (!g_settings.hideCategoryViewOption) {
        return;
    }

    for (auto const& hookedSemanticZoom : g_hookedSemanticZooms) {
        if (hookedSemanticZoom.get() == semanticZoom) {
            return;
        }
    }

    g_hookedSemanticZooms.push_back(winrt::make_weak(semanticZoom));

    semanticZoom.CanChangeViews(false);
    semanticZoom.IsZoomOutButtonEnabled(false);
    if (!semanticZoom.IsZoomedInViewActive()) {
        semanticZoom.IsZoomedInViewActive(true);
    }

    semanticZoom.RegisterPropertyChangedCallback(
        wuc::SemanticZoom::CanChangeViewsProperty(),
        [](wux::DependencyObject const& sender, wux::DependencyProperty const&) {
            if (auto semanticZoom = sender.try_as<wuc::SemanticZoom>()) {
                if (semanticZoom.CanChangeViews()) {
                    semanticZoom.CanChangeViews(false);
                }
            }
        });

    semanticZoom.RegisterPropertyChangedCallback(
        wuc::SemanticZoom::IsZoomOutButtonEnabledProperty(),
        [](wux::DependencyObject const& sender, wux::DependencyProperty const&) {
            if (auto semanticZoom = sender.try_as<wuc::SemanticZoom>()) {
                if (semanticZoom.IsZoomOutButtonEnabled()) {
                    semanticZoom.IsZoomOutButtonEnabled(false);
                }
            }
        });

    semanticZoom.RegisterPropertyChangedCallback(
        wuc::SemanticZoom::IsZoomedInViewActiveProperty(),
        [](wux::DependencyObject const& sender, wux::DependencyProperty const&) {
            if (auto semanticZoom = sender.try_as<wuc::SemanticZoom>()) {
                if (!semanticZoom.IsZoomedInViewActive()) {
                    semanticZoom.IsZoomedInViewActive(true);
                }
            }
        });
}

void HideStartMenuElement(InstanceHandle handle,
                          wux::FrameworkElement const& element,
                          PCWSTR type) {
    ApplyHeaderText(element);

    FlattenAllAppsGridSource(element, type);

    if (auto semanticZoom = element.try_as<wuc::SemanticZoom>()) {
        DisableCategorySemanticZoom(semanticZoom);
    }

    if (g_settings.hideCategoryViewOption) {
        if (auto categoryViewOption = FindCategoryViewOptionTarget(element)) {
            CollapseElement(categoryViewOption);
            KeepElementCollapsed(categoryViewOption);
            return;
        }
    }

    if (g_settings.hideScrollBar && element.try_as<wucp::ScrollBar>() &&
        IsInAllAppsArea(element)) {
        CollapseElement(element);
        KeepElementCollapsed(element);
        return;
    }

    if (IsGroupHeaderElement(element, type) ||
        IsPinnedSectionElement(element, type) ||
        element.Name() == L"TopLevelSuggestionsRoot" ||
        (g_settings.hideTopLevelHeader && element.Name() == L"TopLevelHeader")) {
        CollapseElement(element);
        KeepElementCollapsed(element);
    }

    if (auto itemsWrapGrid = element.try_as<wuc::ItemsWrapGrid>()) {
        CompactAllAppsItemsWrapGrid(itemsWrapGrid);
        FlattenAllAppsGridSourceFromAncestor(element);
    } else if (IsGroupHeaderElement(element, type)) {
        FlattenAllAppsGridSourceFromAncestor(element);
    }
}

class VisualTreeWatcher : public winrt::implements<VisualTreeWatcher, IVisualTreeServiceCallback2, winrt::non_agile> {
public:
    VisualTreeWatcher(winrt::com_ptr<IUnknown> site) {
        m_XamlDiagnostics = site.as<IXamlDiagnostics>();
        HANDLE thread = CreateThread(
            nullptr, 0,
            [](LPVOID lpParam) -> DWORD {
                auto watcher = reinterpret_cast<VisualTreeWatcher*>(lpParam);
                HRESULT hr = watcher->m_XamlDiagnostics.as<IVisualTreeService3>()->AdviseVisualTreeChange(watcher);
                watcher->Release();
                if (FAILED(hr)) {
                    Wh_Log(L"AdviseVisualTreeChange failed: %08X", hr);
                }
                return 0;
            },
            this, 0, nullptr);
        if (thread) {
            AddRef();
            CloseHandle(thread);
        }
    }

    void UnadviseVisualTreeChange() {
        HRESULT hr = m_XamlDiagnostics.as<IVisualTreeService3>()->UnadviseVisualTreeChange(this);
        if (FAILED(hr)) {
            Wh_Log(L"UnadviseVisualTreeChange failed: %08X", hr);
        }
    }

private:
    wf::IInspectable FromHandle(InstanceHandle handle) {
        wf::IInspectable obj;
        winrt::check_hresult(m_XamlDiagnostics->GetIInspectableFromHandle(
            handle, reinterpret_cast<::IInspectable**>(winrt::put_abi(obj))));
        return obj;
    }

    HRESULT STDMETHODCALLTYPE OnVisualTreeChange(ParentChildRelation, VisualElement element, VisualMutationType mutationType) override try {
        if (g_targetThreadId && GetCurrentThreadId() != g_targetThreadId) {
            return S_OK;
        }

        if (mutationType == Add) {
            if (auto frameworkElement = FromHandle(element.Handle).try_as<wux::FrameworkElement>()) {
                HideStartMenuElement(element.Handle, frameworkElement, element.Type);
            }
        }

        return S_OK;
    } catch (...) {
        Wh_Log(L"OnVisualTreeChange error: %08X", winrt::to_hresult());
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnElementStateChanged(InstanceHandle, VisualElementState, LPCWSTR) noexcept override {
        return S_OK;
    }

    winrt::com_ptr<IXamlDiagnostics> m_XamlDiagnostics = nullptr;
};

winrt::com_ptr<VisualTreeWatcher> g_visualTreeWatcher;

// {C85D8CC7-5463-40E8-A432-F5916B6427E5}
static constexpr CLSID CLSID_WindhawkTAP = {
    0xc85d8cc7, 0x5463, 0x40e8, {0xa4, 0x32, 0xf5, 0x91, 0x6b, 0x64, 0x27, 0xe5}};

class WindhawkTAP : public winrt::implements<WindhawkTAP, IObjectWithSite, winrt::non_agile> {
public:
    HRESULT STDMETHODCALLTYPE SetSite(IUnknown* pUnkSite) override try {
        if (g_visualTreeWatcher) {
            g_visualTreeWatcher->UnadviseVisualTreeChange();
            g_visualTreeWatcher = nullptr;
        }

        m_site.copy_from(pUnkSite);
        if (m_site) {
            FreeLibrary(GetCurrentModuleHandle());
            g_visualTreeWatcher = winrt::make_self<VisualTreeWatcher>(m_site);
        }

        return S_OK;
    } catch (...) {
        return winrt::to_hresult();
    }

    HRESULT STDMETHODCALLTYPE GetSite(REFIID riid, void** ppvSite) noexcept override {
        return m_site.as(riid, ppvSite);
    }

private:
    winrt::com_ptr<IUnknown> m_site;
};

template <class T>
struct SimpleFactory : winrt::implements<SimpleFactory<T>, IClassFactory, winrt::non_agile> {
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppvObject) override try {
        if (pUnkOuter) {
            return CLASS_E_NOAGGREGATION;
        }

        *ppvObject = nullptr;
        return winrt::make<T>().as(riid, ppvObject);
    } catch (...) {
        return winrt::to_hresult();
    }

    HRESULT STDMETHODCALLTYPE LockServer(BOOL) noexcept override {
        return S_OK;
    }
};

__declspec(dllexport) STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) try {
    if (rclsid == CLSID_WindhawkTAP) {
        *ppv = nullptr;
        return winrt::make<SimpleFactory<WindhawkTAP>>().as(riid, ppv);
    }

    return CLASS_E_CLASSNOTAVAILABLE;
} catch (...) {
    return winrt::to_hresult();
}

__declspec(dllexport) STDAPI DllCanUnloadNow() {
    return winrt::get_module_lock() ? S_FALSE : S_OK;
}

using PFN_INITIALIZE_XAML_DIAGNOSTICS_EX = decltype(&InitializeXamlDiagnosticsEx);

HRESULT InjectWindhawkTAP() noexcept {
    HMODULE module = GetCurrentModuleHandle();
    if (!module) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    WCHAR location[MAX_PATH];
    switch (GetModuleFileName(module, location, ARRAYSIZE(location))) {
        case 0:
        case ARRAYSIZE(location):
            return HRESULT_FROM_WIN32(GetLastError());
    }

    HMODULE wuxModule = LoadLibraryEx(L"Windows.UI.Xaml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!wuxModule) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    auto initializeXamlDiagnosticsEx =
        reinterpret_cast<PFN_INITIALIZE_XAML_DIAGNOSTICS_EX>(
            GetProcAddress(wuxModule, "InitializeXamlDiagnosticsEx"));
    if (!initializeXamlDiagnosticsEx) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    HRESULT hr = E_FAIL;
    for (int i = 0; i < 10000; i++) {
        WCHAR connectionName[256];
        wsprintf(connectionName, L"VisualDiagConnection%d", i + 1);

        hr = initializeXamlDiagnosticsEx(connectionName, GetCurrentProcessId(), L"", location, CLSID_WindhawkTAP, nullptr);
        if (hr != HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) {
            break;
        }
    }

    return hr;
}

void InitializeXamlWatcher() {
    DWORD noThreadId = 0;
    if (!g_targetThreadId.compare_exchange_strong(noThreadId, GetCurrentThreadId())) {
        return;
    }

    HRESULT hr = InjectWindhawkTAP();
    if (FAILED(hr)) {
        Wh_Log(L"InjectWindhawkTAP failed: %08X", hr);
        g_targetThreadId = 0;
    }
}

using RunFromWindowThreadProc_t = void(WINAPI*)(PVOID parameter);

bool RunFromWindowThread(HWND hWnd, RunFromWindowThreadProc_t proc, PVOID procParam) {
    static const UINT runFromWindowThreadRegisteredMsg =
        RegisterWindowMessage(L"Windhawk_RunFromWindowThread_" WH_MOD_ID);

    struct RUN_FROM_WINDOW_THREAD_PARAM {
        RunFromWindowThreadProc_t proc;
        PVOID procParam;
    };

    DWORD dwThreadId = GetWindowThreadProcessId(hWnd, nullptr);
    if (dwThreadId == 0) {
        return false;
    }

    if (dwThreadId == GetCurrentThreadId()) {
        proc(procParam);
        return true;
    }

    HHOOK hook = SetWindowsHookEx(
        WH_CALLWNDPROC,
        [](int nCode, WPARAM wParam, LPARAM lParam) -> LRESULT {
            if (nCode == HC_ACTION) {
                const CWPSTRUCT* cwp = (const CWPSTRUCT*)lParam;
                if (cwp->message == runFromWindowThreadRegisteredMsg) {
                    auto* param = (RUN_FROM_WINDOW_THREAD_PARAM*)cwp->lParam;
                    param->proc(param->procParam);
                }
            }

            return CallNextHookEx(nullptr, nCode, wParam, lParam);
        },
        nullptr, dwThreadId);
    if (!hook) {
        return false;
    }

    RUN_FROM_WINDOW_THREAD_PARAM param{proc, procParam};
    SendMessage(hWnd, runFromWindowThreadRegisteredMsg, 0, (LPARAM)&param);
    UnhookWindowsHookEx(hook);

    return true;
}

HWND GetCoreWnd() {
    HWND coreWnd = nullptr;
    EnumWindows(
        [](HWND hWnd, LPARAM lParam) -> BOOL {
            DWORD dwProcessId = 0;
            if (!GetWindowThreadProcessId(hWnd, &dwProcessId) ||
                dwProcessId != GetCurrentProcessId()) {
                return TRUE;
            }

            WCHAR className[32];
            if (!GetClassName(hWnd, className, ARRAYSIZE(className))) {
                return TRUE;
            }

            if (_wcsicmp(className, L"Windows.UI.Core.CoreWindow") == 0) {
                *(HWND*)lParam = hWnd;
                return FALSE;
            }

            return TRUE;
        },
        (LPARAM)&coreWnd);

    return coreWnd;
}

void OnWindowCreated(HWND hWnd, LPCWSTR lpClassName) {
    BOOL textualClassName = ((ULONG_PTR)lpClassName & ~(ULONG_PTR)0xffff) != 0;
    if (!textualClassName ||
        _wcsicmp(lpClassName, L"Windows.UI.Core.CoreWindow") != 0) {
        return;
    }

    RunFromWindowThread(hWnd, [](PVOID) { InitializeXamlWatcher(); }, nullptr);
}

using CreateWindowInBand_t = HWND(WINAPI*)(DWORD dwExStyle,
                                           LPCWSTR lpClassName,
                                           LPCWSTR lpWindowName,
                                           DWORD dwStyle,
                                           int X,
                                           int Y,
                                           int nWidth,
                                           int nHeight,
                                           HWND hWndParent,
                                           HMENU hMenu,
                                           HINSTANCE hInstance,
                                           PVOID lpParam,
                                           DWORD dwBand);
CreateWindowInBand_t CreateWindowInBand_Original;

HWND WINAPI CreateWindowInBand_Hook(DWORD dwExStyle,
                                    LPCWSTR lpClassName,
                                    LPCWSTR lpWindowName,
                                    DWORD dwStyle,
                                    int X,
                                    int Y,
                                    int nWidth,
                                    int nHeight,
                                    HWND hWndParent,
                                    HMENU hMenu,
                                    HINSTANCE hInstance,
                                    PVOID lpParam,
                                    DWORD dwBand) {
    HWND hWnd = CreateWindowInBand_Original(
        dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight,
        hWndParent, hMenu, hInstance, lpParam, dwBand);
    if (hWnd) {
        OnWindowCreated(hWnd, lpClassName);
    }

    return hWnd;
}

using CreateWindowInBandEx_t = HWND(WINAPI*)(DWORD dwExStyle,
                                             LPCWSTR lpClassName,
                                             LPCWSTR lpWindowName,
                                             DWORD dwStyle,
                                             int X,
                                             int Y,
                                             int nWidth,
                                             int nHeight,
                                             HWND hWndParent,
                                             HMENU hMenu,
                                             HINSTANCE hInstance,
                                             PVOID lpParam,
                                             DWORD dwBand,
                                             DWORD dwTypeFlags);
CreateWindowInBandEx_t CreateWindowInBandEx_Original;

HWND WINAPI CreateWindowInBandEx_Hook(DWORD dwExStyle,
                                      LPCWSTR lpClassName,
                                      LPCWSTR lpWindowName,
                                      DWORD dwStyle,
                                      int X,
                                      int Y,
                                      int nWidth,
                                      int nHeight,
                                      HWND hWndParent,
                                      HMENU hMenu,
                                      HINSTANCE hInstance,
                                      PVOID lpParam,
                                      DWORD dwBand,
                                      DWORD dwTypeFlags) {
    HWND hWnd = CreateWindowInBandEx_Original(
        dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight,
        hWndParent, hMenu, hInstance, lpParam, dwBand, dwTypeFlags);
    if (hWnd) {
        OnWindowCreated(hWnd, lpClassName);
    }

    return hWnd;
}

BOOL Wh_ModInit() {
    LoadSettings();

    HMODULE user32Module =
        LoadLibraryEx(L"user32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (user32Module) {
        if (void* createWindowInBand =
                (void*)GetProcAddress(user32Module, "CreateWindowInBand")) {
            Wh_SetFunctionHook(createWindowInBand, (void*)CreateWindowInBand_Hook,
                               (void**)&CreateWindowInBand_Original);
        }

        if (void* createWindowInBandEx =
                (void*)GetProcAddress(user32Module, "CreateWindowInBandEx")) {
            Wh_SetFunctionHook(createWindowInBandEx,
                               (void*)CreateWindowInBandEx_Hook,
                               (void**)&CreateWindowInBandEx_Original);
        }
    }

    return TRUE;
}

void Wh_ModAfterInit() {
    HWND coreWnd = GetCoreWnd();
    if (coreWnd) {
        RunFromWindowThread(coreWnd, [](PVOID) { InitializeXamlWatcher(); }, nullptr);
    } else {
        InitializeXamlWatcher();
    }
}

void Wh_ModUninit() {
    if (g_visualTreeWatcher) {
        g_visualTreeWatcher->UnadviseVisualTreeChange();
        g_visualTreeWatcher = nullptr;
    }

    g_flattenedSources.clear();
    g_hookedSemanticZooms.clear();
    g_gapAdjustedItemsWrapGrids.clear();
    g_targetThreadId = 0;
}

void Wh_ModSettingsChanged() {
    LoadSettings();
    ExitProcess(0);
}
