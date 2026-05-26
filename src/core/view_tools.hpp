#pragma once

#include "core/content_registry.hpp"
#include "core/view.hpp"

#include <map>
#include <vector>

namespace hex::plugin::builtin {

    class ViewTools : public View::Scrolling {
    public:
        ViewTools();
        ~ViewTools() override = default;

        void drawContent() override;
        void drawAlwaysVisibleContent() override;
        void drawHelpText() override;

    private:
        std::vector<ContentRegistry::Tools::impl::Entry>::const_iterator m_dragStartIterator;
        std::map<UnlocalizedString, bool> m_detachedTools;
        std::map<UnlocalizedString, bool> m_collapsedTools;
    };

}
