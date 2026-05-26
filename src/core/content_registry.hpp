#pragma once

#include "core/unlocalized_string.hpp"
#include "core/view.hpp"

#include <functional>
#include <map>
#include <memory>
#include <type_traits>
#include <vector>

namespace hex {

    namespace ContentRegistry::Views {

        namespace impl {
            void add(std::unique_ptr<View>&& view);
            void setFullScreenView(std::unique_ptr<View>&& view);
            void remove(const UnlocalizedString& unlocalizedName);
            void clear();

            const std::map<UnlocalizedString, std::unique_ptr<View>>& getEntries();
            const std::unique_ptr<View>& getFullScreenView();
        }

        template<typename T, typename... Args>
        void add(Args&&... args) {
            static_assert(std::is_base_of<View, T>::value, "View registry entries must inherit from hex::View");
            return impl::add(std::make_unique<T>(std::forward<Args>(args)...));
        }

        template<typename T, typename... Args>
        void setFullScreenView(Args&&... args) {
            static_assert(std::is_base_of<View, T>::value, "Fullscreen view must inherit from hex::View");
            return impl::setFullScreenView(std::make_unique<T>(std::forward<Args>(args)...));
        }

        View* getViewByName(const UnlocalizedString& unlocalizedName);
        View* getFocusedView();
    }

    namespace ContentRegistry::Tools {

        namespace impl {
            using Callback = std::function<void()>;

            struct Entry {
                UnlocalizedString unlocalizedName;
                const char* icon;
                Callback function;
            };

            const std::vector<Entry>& getEntries();
        }

        void add(const UnlocalizedString& unlocalizedName, const char* icon, const impl::Callback& function);
        void clear();
    }

}
