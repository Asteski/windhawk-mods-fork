// ==WindhawkMod==
// @id              compact-start-menu
// @name            Compact Start Menu
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
- headerText: "Installed apps"
  $name: Change Header Text
  $description: Text to show in the All apps header.
- hiddenHeaderIconGap: 16
  $name: Hidden header icon gap
  $description: Top gap, in pixels, between search and the first icon row when the top header is hidden.
- scrollBarMode: "showWhileScrolling"
  $name: Scroll bar
  $description: Controls All apps scroll bar visibility.
  $options:
  - show: Show
  - hide: Hide
  - showWhileScrolling: Show while scrolling
- SectionsAndHeaders:
    - hidePinnedSection: true
      $name: Hide pinned section
      $description: Hide the Start menu pinned apps section.
    - hidePinnedHeader: true
      $name: Hide pinned header
      $description: Hide the Pinned section header.
    - hideRecommendedSection: true
      $name: Hide recommended section
      $description: Hide the Start menu recommended section.
    - hideRecommendedHeader: true
      $name: Hide recommended header
      $description: Hide the Recommended section header.
  $name: Other Sections
*/
// ==/WindhawkModSettings==

// ==WindhawkModReadme==
/*
# Compact Start Menu Grid
This mod simplifies the Start Menu by directly adjusting All apps XAML sections.

### Features:
* **Shows "All apps":** Keeps the app list visible.
* **Optional pinned section hiding:** Can hide or show the section with pinned apps.
* **Optional recommended section hiding:** Can hide or show the section with recent files.
* **Optional section headers:** Can hide pinned and recommended headers independently.
* **Hides group headers:** Removes the visible All apps group headers in grid and list views.
* **Compacts grid view:** Reduces group-header spacing in the All apps grid.
* **Optional top header:** Can hide the All apps title/view row.
* **Optional category view hiding:** Can hide the Category option from the view selector.
* **Hidden header icon gap:** Can adjust the first icon row spacing when the top header is hidden.
* **Scrollbar modes:** Can show, hide, or reveal the visual All apps scroll bar only while scrolling.

### Instructions:
* Enable the mod in Windhawk.
* **Restart StartMenuExperienceHost.exe**, sign out/in, or restart your computer to apply changes.
*/
// ==/WindhawkModReadme==

#include <windhawk_utils.h>
#include <atomic>
#include <chrono>
#include <string>
#include <vector>

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

enum class ScrollBarMode {
    Show,
    Hide,
    ShowWhileScrolling,
};

struct Settings {
    bool hidePinnedSection = true;
    bool hidePinnedHeader = true;
    bool hideRecommendedSection = true;
    bool hideRecommendedHeader = true;
    bool hideTopLevelHeader = true;
    std::wstring headerText = L"Installed apps";
    bool hideCategoryViewOption = true;
    int hiddenHeaderIconGap = 16;
    ScrollBarMode scrollBarMode = ScrollBarMode::Show;
};

Settings g_settings;

std::atomic<bool> g_xamlTraversalInstalled = false;
thread_local bool g_processingXamlElement = false;

struct FlattenedSourceEntry {
    winrt::weak_ref<wuc::ItemsControl> itemsControl;
    wf::IInspectable originalSource{nullptr};
    wfc::IObservableVector<wf::IInspectable> flatSource{nullptr};
};

std::vector<FlattenedSourceEntry> g_flattenedSources;
std::vector<winrt::weak_ref<wuc::SemanticZoom>> g_hookedSemanticZooms;
std::vector<winrt::weak_ref<wuc::ItemsWrapGrid>> g_gapAdjustedItemsWrapGrids;
std::vector<winrt::weak_ref<wux::FrameworkElement>> g_keepCollapsedElements;
std::vector<winrt::weak_ref<wuc::TextBlock>> g_headerTextElements;
std::vector<winrt::weak_ref<wuc::ScrollViewer>> g_scrollBarModeScrollViewers;
std::vector<winrt::weak_ref<wucp::ScrollBar>> g_scrollBarModeScrollBars;
wux::DispatcherTimer g_xamlTraversalTimer{nullptr};
wux::DispatcherTimer g_scrollBarHideTimer{nullptr};
wux::DispatcherTimer g_scrollBarFadeTimer{nullptr};

void LoadSettings() {
    g_settings.hidePinnedSection =
        Wh_GetIntSetting(L"SectionsAndHeaders.hidePinnedSection") != 0;
    g_settings.hidePinnedHeader =
        Wh_GetIntSetting(L"SectionsAndHeaders.hidePinnedHeader") != 0;
    g_settings.hideRecommendedSection =
        Wh_GetIntSetting(L"SectionsAndHeaders.hideRecommendedSection") != 0;
    g_settings.hideRecommendedHeader =
        Wh_GetIntSetting(L"SectionsAndHeaders.hideRecommendedHeader") != 0;
    g_settings.hideTopLevelHeader = Wh_GetIntSetting(L"hideTopLevelHeader") != 0;

    PCWSTR headerText = Wh_GetStringSetting(L"headerText");
    g_settings.headerText =
        headerText && *headerText ? headerText : L"Installed apps";
    Wh_FreeStringSetting(headerText);

    g_settings.hideCategoryViewOption =
        Wh_GetIntSetting(L"hideCategoryViewOption") != 0;
    g_settings.hiddenHeaderIconGap = Wh_GetIntSetting(L"hiddenHeaderIconGap");
    if (g_settings.hiddenHeaderIconGap < 0) {
        g_settings.hiddenHeaderIconGap = 0;
    }

    PCWSTR scrollBarMode = Wh_GetStringSetting(L"scrollBarMode");
    if (scrollBarMode && lstrcmpiW(scrollBarMode, L"hide") == 0) {
        g_settings.scrollBarMode = ScrollBarMode::Hide;
    } else if (scrollBarMode &&
               lstrcmpiW(scrollBarMode, L"showWhileScrolling") == 0) {
        g_settings.scrollBarMode = ScrollBarMode::ShowWhileScrolling;
    } else if ((!scrollBarMode || !*scrollBarMode) &&
               Wh_GetIntSetting(L"hideScrollBar") != 0) {
        g_settings.scrollBarMode = ScrollBarMode::Hide;
    } else {
        g_settings.scrollBarMode = ScrollBarMode::Show;
    }
    Wh_FreeStringSetting(scrollBarMode);
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
    auto className = winrt::get_class_name(element);

    return name == L"StartMenuPinnedList" ||
           name == L"PinnedList" ||
           name == L"PinnedListPipsPager" ||
           className == L"StartMenu.PinnedList" ||
           (type && lstrcmpiW(type, L"StartMenu.PinnedList") == 0);
}

bool IsPinnedHeaderElement(wux::FrameworkElement const& element) {
    auto name = element.Name();
    return name == L"PinnedListHeaderGrid" ||
           name == L"PinnedListHeaderText" ||
           name == L"ShowMorePinnedGrid" ||
           name == L"ShowMorePinnedButton" ||
           name == L"ShowMorePinnedButtonText";
}

bool IsRecommendedHeaderElement(wux::FrameworkElement const& element) {
    auto name = element.Name();
    return name == L"TopLevelSuggestionsListHeader" ||
           name == L"TopLevelSuggestionsListHeaderText" ||
           name == L"MoreSuggestionsListHeaderText" ||
           name == L"ShowMoreSuggestions" ||
           name == L"ShowMoreSuggestionsButton" ||
           name == L"HideMoreSuggestionsButton";
}

bool IsAllAppsTopHeaderElement(wux::FrameworkElement const& element) {
    auto name = element.Name();
    return name == L"AllAppsPaneHeader" ||
           name == L"AllListHeading" ||
           name == L"AllListHeadingText" ||
           name == L"ViewSelectionButton";
}

bool IsGroupHeaderElement(wux::FrameworkElement const& element, PCWSTR type) {
    auto className = winrt::get_class_name(element);

    return element.try_as<wuc::GridViewHeaderItem>() ||
           element.try_as<wuc::ListViewHeaderItem>() ||
           className == L"Windows.UI.Xaml.Controls.GridViewHeaderItem" ||
           className == L"Windows.UI.Xaml.Controls.ListViewHeaderItem" ||
           (type && (lstrcmpiW(type, L"Windows.UI.Xaml.Controls.GridViewHeaderItem") == 0 ||
                     lstrcmpiW(type, L"Windows.UI.Xaml.Controls.ListViewHeaderItem") == 0));
}

void CollapseElement(wux::FrameworkElement const& element) {
    if (element.Visibility() != wux::Visibility::Collapsed) {
        element.Visibility(wux::Visibility::Collapsed);
    }
    if (element.Width() != 0) {
        element.Width(0);
    }
    if (element.MinWidth() != 0) {
        element.MinWidth(0);
    }
    if (element.MaxWidth() != 0) {
        element.MaxWidth(0);
    }
    if (element.MinHeight() != 0) {
        element.MinHeight(0);
    }
    if (element.Height() != 0) {
        element.Height(0);
    }
    if (element.MaxHeight() != 0) {
        element.MaxHeight(0);
    }
    if (element.Margin() != wux::Thickness{}) {
        element.Margin({});
    }
    if (element.HorizontalAlignment() != wux::HorizontalAlignment::Left) {
        element.HorizontalAlignment(wux::HorizontalAlignment::Left);
    }
    if (element.VerticalAlignment() != wux::VerticalAlignment::Top) {
        element.VerticalAlignment(wux::VerticalAlignment::Top);
    }
    if (element.Opacity() != 0) {
        element.Opacity(0);
    }
    if (element.IsHitTestVisible()) {
        element.IsHitTestVisible(false);
    }
}

void KeepElementCollapsed(wux::FrameworkElement const& element) {
    for (auto const& collapsedElement : g_keepCollapsedElements) {
        if (collapsedElement.get() == element) {
            return;
        }
    }

    g_keepCollapsedElements.push_back(winrt::make_weak(element));

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
        if (textBlock.Text() != g_settings.headerText) {
            textBlock.Text(g_settings.headerText);
        }

        for (auto const& headerTextElement : g_headerTextElements) {
            if (headerTextElement.get() == textBlock) {
                return;
            }
        }

        g_headerTextElements.push_back(winrt::make_weak(textBlock));

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

std::vector<wf::IInspectable> GetFlatItemsFromGroupedSource(
    wf::IInspectable const& source) {
    std::vector<wf::IInspectable> flatItems;

    auto collectionView = source.try_as<wuxd::ICollectionView>();
    if (!collectionView) {
        return flatItems;
    }

    auto groups = collectionView.CollectionGroups();
    if (!groups || groups.Size() == 0) {
        return flatItems;
    }

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

    return flatItems;
}

bool FlatSourceMatchesItems(
    wfc::IObservableVector<wf::IInspectable> const& flatSource,
    std::vector<wf::IInspectable> const& flatItems) {
    if (!flatSource || flatSource.Size() != flatItems.size()) {
        return false;
    }

    for (uint32_t i = 0; i < flatItems.size(); i++) {
        if (flatSource.GetAt(i) != flatItems[i]) {
            return false;
        }
    }

    return true;
}

void ReplaceFlatSourceItems(
    wfc::IObservableVector<wf::IInspectable> const& flatSource,
    std::vector<wf::IInspectable> const& flatItems) {
    flatSource.Clear();
    for (auto const& item : flatItems) {
        flatSource.Append(item);
    }
}

void RefreshFlattenedSource(FlattenedSourceEntry& entry) {
    auto itemsControl = entry.itemsControl.get();
    if (!itemsControl || !entry.originalSource) {
        return;
    }

    auto currentSource = itemsControl.ItemsSource();
    if (entry.flatSource && currentSource != entry.flatSource) {
        entry.originalSource = currentSource;
    }

    auto flatItems = GetFlatItemsFromGroupedSource(entry.originalSource);
    if (flatItems.empty()) {
        return;
    }

    if (!entry.flatSource) {
        entry.flatSource =
            winrt::single_threaded_observable_vector<wf::IInspectable>();
    }

    if (!FlatSourceMatchesItems(entry.flatSource, flatItems)) {
        ReplaceFlatSourceItems(entry.flatSource, flatItems);
    }

    if (itemsControl.ItemsSource() != entry.flatSource) {
        itemsControl.ItemsSource(entry.flatSource);
    }

    if (itemsControl.GroupStyle().Size() > 0) {
        itemsControl.GroupStyle().Clear();
    }
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

    for (auto& entry : g_flattenedSources) {
        if (entry.itemsControl.get() == itemsControl) {
            RefreshFlattenedSource(entry);
            return;
        }
    }

    auto originalSource = itemsControl.ItemsSource();
    auto flatItems = GetFlatItemsFromGroupedSource(originalSource);
    if (flatItems.empty()) {
        return;
    }

    auto flatSource = winrt::single_threaded_observable_vector(std::move(flatItems));
    itemsControl.ItemsSource(flatSource);
    if (itemsControl.GroupStyle().Size() > 0) {
        itemsControl.GroupStyle().Clear();
    }

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

void SetScrollViewerVerticalScrollBarVisibility(
    wuc::ScrollViewer const& scrollViewer,
    wuc::ScrollBarVisibility visibility) {
    if (scrollViewer.VerticalScrollBarVisibility() != visibility) {
        scrollViewer.VerticalScrollBarVisibility(visibility);
    }
}

void SetScrollBarOpacity(wucp::ScrollBar const& scrollBar, double opacity) {
    if (scrollBar.Opacity() != opacity) {
        scrollBar.Opacity(opacity);
    }

    bool visible = opacity > 0;
    if (scrollBar.IsHitTestVisible() != visible) {
        scrollBar.IsHitTestVisible(visible);
    }
}

void StopScrollBarFadeTimer() {
    if (g_scrollBarFadeTimer) {
        g_scrollBarFadeTimer.Stop();
    }
}

void FadeAutoScrollBars() {
    if (g_settings.scrollBarMode != ScrollBarMode::ShowWhileScrolling) {
        return;
    }

    bool stillFading = false;
    for (auto const& weakScrollBar : g_scrollBarModeScrollBars) {
        if (auto scrollBar = weakScrollBar.get()) {
            double opacity = scrollBar.Opacity();
            double nextOpacity = opacity > 0.2 ? opacity - 0.2 : 0;
            SetScrollBarOpacity(scrollBar, nextOpacity);
            if (nextOpacity > 0) {
                stillFading = true;
            }
        }
    }

    if (!stillFading) {
        StopScrollBarFadeTimer();
    }
}

void StartScrollBarFadeTimer() {
    if (!g_scrollBarFadeTimer) {
        g_scrollBarFadeTimer = wux::DispatcherTimer();
        g_scrollBarFadeTimer.Interval(std::chrono::milliseconds(50));
        g_scrollBarFadeTimer.Tick(
            [](wf::IInspectable const&, wf::IInspectable const&) {
                FadeAutoScrollBars();
            });
    }

    g_scrollBarFadeTimer.Stop();
    g_scrollBarFadeTimer.Start();
}

void RestartScrollBarHideTimer() {
    if (!g_scrollBarHideTimer) {
        g_scrollBarHideTimer = wux::DispatcherTimer();
        g_scrollBarHideTimer.Interval(std::chrono::milliseconds(200));
        g_scrollBarHideTimer.Tick(
            [](wf::IInspectable const&, wf::IInspectable const&) {
                if (g_scrollBarHideTimer) {
                    g_scrollBarHideTimer.Stop();
                }
                StartScrollBarFadeTimer();
            });
    }

    g_scrollBarHideTimer.Stop();
    g_scrollBarHideTimer.Start();
}

void ShowAutoScrollBarsWhileScrolling(wuc::ScrollViewer const& scrollViewer) {
    if (g_settings.scrollBarMode != ScrollBarMode::ShowWhileScrolling) {
        return;
    }

    StopScrollBarFadeTimer();
    SetScrollViewerVerticalScrollBarVisibility(
        scrollViewer, wuc::ScrollBarVisibility::Visible);

    for (auto const& weakScrollBar : g_scrollBarModeScrollBars) {
        if (auto scrollBar = weakScrollBar.get()) {
            SetScrollBarOpacity(scrollBar, 1);
        }
    }

    RestartScrollBarHideTimer();
}

void ApplyScrollBarElementMode(wucp::ScrollBar const& scrollBar) {
    if (!IsInAllAppsArea(scrollBar)) {
        return;
    }

    if (g_settings.scrollBarMode == ScrollBarMode::Show) {
        SetScrollBarOpacity(scrollBar, 1);
        return;
    }

    if (g_settings.scrollBarMode == ScrollBarMode::Hide) {
        SetScrollBarOpacity(scrollBar, 0);
        return;
    }

    for (auto const& weakScrollBar : g_scrollBarModeScrollBars) {
        if (weakScrollBar.get() == scrollBar) {
            return;
        }
    }

    g_scrollBarModeScrollBars.push_back(winrt::make_weak(scrollBar));
    SetScrollBarOpacity(scrollBar, 0);
}

void ApplyScrollBarMode(wuc::ScrollViewer const& scrollViewer) {
    if (!IsInAllAppsArea(scrollViewer)) {
        return;
    }

    if (g_settings.scrollBarMode == ScrollBarMode::Show) {
        SetScrollViewerVerticalScrollBarVisibility(
            scrollViewer, wuc::ScrollBarVisibility::Auto);
        return;
    }

    if (g_settings.scrollBarMode == ScrollBarMode::Hide) {
        SetScrollViewerVerticalScrollBarVisibility(
            scrollViewer, wuc::ScrollBarVisibility::Hidden);
        return;
    }

    for (auto const& weakScrollViewer : g_scrollBarModeScrollViewers) {
        if (weakScrollViewer.get() == scrollViewer) {
            return;
        }
    }

    g_scrollBarModeScrollViewers.push_back(winrt::make_weak(scrollViewer));

    SetScrollViewerVerticalScrollBarVisibility(
        scrollViewer, wuc::ScrollBarVisibility::Visible);

    scrollViewer.ViewChanged(
        [](wf::IInspectable const& sender,
           wuc::ScrollViewerViewChangedEventArgs const&) {
            if (auto scrollViewer = sender.try_as<wuc::ScrollViewer>()) {
                ShowAutoScrollBarsWhileScrolling(scrollViewer);
            }
        });
}

void HideStartMenuElement(wux::FrameworkElement const& element,
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

    if (auto scrollViewer = element.try_as<wuc::ScrollViewer>()) {
        ApplyScrollBarMode(scrollViewer);
    }

    if (auto scrollBar = element.try_as<wucp::ScrollBar>()) {
        ApplyScrollBarElementMode(scrollBar);
    }

    if (IsGroupHeaderElement(element, type) ||
        (g_settings.hidePinnedSection &&
         IsPinnedSectionElement(element, type)) ||
        ((g_settings.hidePinnedSection || g_settings.hidePinnedHeader) &&
         IsPinnedHeaderElement(element)) ||
        (g_settings.hideRecommendedSection &&
         element.Name() == L"TopLevelSuggestionsRoot") ||
        ((g_settings.hideRecommendedSection ||
          g_settings.hideRecommendedHeader) &&
         IsRecommendedHeaderElement(element)) ||
        (g_settings.hideTopLevelHeader && IsAllAppsTopHeaderElement(element))) {
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

void ProcessXamlElement(wux::FrameworkElement const& element) {
    if (!element || g_processingXamlElement) {
        return;
    }

    g_processingXamlElement = true;

    try {
        HideStartMenuElement(element, nullptr);
    } catch (...) {
        Wh_Log(L"ProcessXamlElement error: %08X", winrt::to_hresult());
    }

    g_processingXamlElement = false;
}

void ProcessXamlSubtree(wux::DependencyObject const& root) {
    if (!root) {
        return;
    }

    std::vector<wux::DependencyObject> stack;
    stack.push_back(root);

    while (!stack.empty()) {
        auto current = stack.back();
        stack.pop_back();

        if (auto element = current.try_as<wux::FrameworkElement>()) {
            ProcessXamlElement(element);
        }

        int childCount = wux::Media::VisualTreeHelper::GetChildrenCount(current);
        for (int i = childCount - 1; i >= 0; i--) {
            auto child = wux::Media::VisualTreeHelper::GetChild(current, i);
            if (child) {
                stack.push_back(child);
            }
        }
    }
}

void ProcessCurrentXamlTree() {
    try {
        auto window = wux::Window::Current();
        if (!window) {
            return;
        }

        auto root = window.Content().try_as<wux::DependencyObject>();
        ProcessXamlSubtree(root);

        auto popups = wux::Media::VisualTreeHelper::GetOpenPopups(window);
        for (uint32_t i = 0; i < popups.Size(); i++) {
            auto popup = popups.GetAt(i);
            if (!popup.IsOpen()) {
                continue;
            }

            if (auto popupChild = popup.Child().try_as<wux::DependencyObject>()) {
                ProcessXamlSubtree(popupChild);
            }
        }
    } catch (...) {
        Wh_Log(L"ProcessCurrentXamlTree error: %08X", winrt::to_hresult());
    }
}

void InstallXamlTraversal() {
    bool expected = false;
    if (!g_xamlTraversalInstalled.compare_exchange_strong(expected, true)) {
        return;
    }

    try {
        ProcessCurrentXamlTree();

        g_xamlTraversalTimer = wux::DispatcherTimer();
        g_xamlTraversalTimer.Interval(std::chrono::milliseconds(250));
        g_xamlTraversalTimer.Tick(
            [](wf::IInspectable const&, wf::IInspectable const&) {
                ProcessCurrentXamlTree();
            });
        g_xamlTraversalTimer.Start();
    } catch (...) {
        g_xamlTraversalInstalled = false;
        g_xamlTraversalTimer = nullptr;
        Wh_Log(L"InstallXamlTraversal error: %08X", winrt::to_hresult());
    }
}

void UninstallXamlTraversal() {
    try {
        if (g_xamlTraversalTimer) {
            g_xamlTraversalTimer.Stop();
            g_xamlTraversalTimer = nullptr;
        }
        if (g_scrollBarHideTimer) {
            g_scrollBarHideTimer.Stop();
            g_scrollBarHideTimer = nullptr;
        }
        if (g_scrollBarFadeTimer) {
            g_scrollBarFadeTimer.Stop();
            g_scrollBarFadeTimer = nullptr;
        }
    } catch (...) {
        Wh_Log(L"UninstallXamlTraversal error: %08X", winrt::to_hresult());
    }

    g_xamlTraversalInstalled = false;
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

    RunFromWindowThread(hWnd, [](PVOID) { InstallXamlTraversal(); }, nullptr);
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
        RunFromWindowThread(coreWnd, [](PVOID) { InstallXamlTraversal(); }, nullptr);
    } else {
        InstallXamlTraversal();
    }
}

void Wh_ModUninit() {
    HWND coreWnd = GetCoreWnd();
    if (coreWnd) {
        RunFromWindowThread(coreWnd, [](PVOID) { UninstallXamlTraversal(); },
                            nullptr);
    } else {
        UninstallXamlTraversal();
    }

    g_flattenedSources.clear();
    g_hookedSemanticZooms.clear();
    g_gapAdjustedItemsWrapGrids.clear();
    g_keepCollapsedElements.clear();
    g_headerTextElements.clear();
    g_scrollBarModeScrollViewers.clear();
    g_scrollBarModeScrollBars.clear();
    g_xamlTraversalInstalled = false;
}

void Wh_ModSettingsChanged() {
    LoadSettings();
    ExitProcess(0);
}
