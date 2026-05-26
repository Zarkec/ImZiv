#include "core/content_registry.hpp"

#include <utility>

namespace hex {

    namespace ContentRegistry::Views {

        namespace impl {
            static std::map<UnlocalizedString, std::unique_ptr<View>> s_views;
            static std::unique_ptr<View> s_fullscreenView;

            const std::map<UnlocalizedString, std::unique_ptr<View>>& getEntries() {
                return s_views;
            }

            void add(std::unique_ptr<View>&& view) {
                s_views.emplace(view->getUnlocalizedName(), std::move(view));
            }

            const std::unique_ptr<View>& getFullScreenView() {
                return s_fullscreenView;
            }

            void setFullScreenView(std::unique_ptr<View>&& view) {
                s_fullscreenView = std::move(view);
            }

            void remove(const UnlocalizedString& unlocalizedName) {
                s_views.erase(unlocalizedName);
            }

            void clear() {
                s_fullscreenView.reset();
                s_views.clear();
            }
        }

        View* getViewByName(const UnlocalizedString& unlocalizedName) {
            auto iter = impl::s_views.find(unlocalizedName);
            if (iter == impl::s_views.end())
                return nullptr;

            return iter->second.get();
        }

        View* getFocusedView() {
            for (const auto& entry : impl::s_views) {
                if (entry.second->isFocused())
                    return entry.second.get();
            }

            return nullptr;
        }

    }

    namespace ContentRegistry::Tools {

        namespace impl {
            static std::vector<Entry> s_tools;

            const std::vector<Entry>& getEntries() {
                return s_tools;
            }
        }

        void add(const UnlocalizedString& unlocalizedName, const char* icon, const impl::Callback& function) {
            impl::s_tools.push_back({ unlocalizedName, icon, function });
        }

        void clear() {
            impl::s_tools.clear();
        }

    }

}
