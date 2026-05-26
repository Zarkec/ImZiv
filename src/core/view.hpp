#pragma once

#include "core/unlocalized_string.hpp"

#include "imgui.h"

#include <cfloat>
#include <map>
#include <string>

namespace hex {

    class View {
        explicit View(UnlocalizedString unlocalizedName, const char* icon);

    public:
        virtual ~View() = default;

        virtual void draw(ImGuiWindowFlags extraFlags = ImGuiWindowFlags_None) = 0;
        virtual void drawContent() = 0;
        virtual void drawAlwaysVisibleContent() {}

        [[nodiscard]] virtual bool shouldDraw() const;
        [[nodiscard]] virtual bool shouldProcess() const;
        [[nodiscard]] virtual bool hasViewMenuItemEntry() const;
        [[nodiscard]] virtual ImVec2 getMinSize() const;
        [[nodiscard]] virtual ImVec2 getMaxSize() const;
        [[nodiscard]] virtual ImGuiWindowFlags getWindowFlags() const;
        [[nodiscard]] virtual View* getMenuItemInheritView() const { return nullptr; }

        [[nodiscard]] const char* getIcon() const { return m_icon; }
        [[nodiscard]] const UnlocalizedString& getUnlocalizedName() const;
        [[nodiscard]] std::string getName() const;
        [[nodiscard]] std::string getWindowTitle() const;

        [[nodiscard]] virtual bool shouldDefaultFocus() const { return false; }
        [[nodiscard]] virtual bool shouldStoreWindowState() const { return true; }

        [[nodiscard]] bool& getWindowOpenState();
        [[nodiscard]] const bool& getWindowOpenState() const;

        [[nodiscard]] bool isFocused() const { return m_focused; }

        [[nodiscard]] static std::string toWindowName(const UnlocalizedString& unlocalizedName);
        [[nodiscard]] static const View* getLastFocusedView();
        static void discardNavigationRequests();

        void bringToFront();

        [[nodiscard]] bool didWindowJustOpen();
        void setWindowJustOpened(bool state);

        [[nodiscard]] bool didWindowJustClose();
        void setWindowJustClosed(bool state);

        void trackViewState();
        void setFocused(bool focused);

    protected:
        virtual void onOpen() {}
        virtual void onClose() {}

    public:
        class Window;
        class Special;
        class Floating;
        class Scrolling;
        class Modal;
        class FullScreen;

    private:
        UnlocalizedString m_unlocalizedViewName;
        bool m_windowOpen = false;
        bool m_prevWindowOpen = false;
        bool m_windowJustOpened = false;
        bool m_windowJustClosed = false;
        const char* m_icon;
        bool m_focused = false;
    };

    class View::Window : public View {
    public:
        explicit Window(UnlocalizedString unlocalizedName, const char* icon)
            : View(std::move(unlocalizedName), icon) {}

        virtual void drawHelpText() = 0;
        void draw(ImGuiWindowFlags extraFlags = ImGuiWindowFlags_None) override;

        [[nodiscard]] virtual bool allowScroll() const {
            return false;
        }
    };

    class View::Special : public View {
    public:
        explicit Special(UnlocalizedString unlocalizedName)
            : View(std::move(unlocalizedName), "") {}

        void draw(ImGuiWindowFlags extraFlags = ImGuiWindowFlags_None) final;
    };

    class View::Floating : public View::Window {
    public:
        explicit Floating(UnlocalizedString unlocalizedName, const char* icon)
            : Window(std::move(unlocalizedName), icon) {}

        void draw(ImGuiWindowFlags extraFlags = ImGuiWindowFlags_None) final;

        [[nodiscard]] bool shouldStoreWindowState() const override { return false; }
    };

    class View::Scrolling : public View::Window {
    public:
        explicit Scrolling(UnlocalizedString unlocalizedName, const char* icon)
            : Window(std::move(unlocalizedName), icon) {}

        void draw(ImGuiWindowFlags extraFlags = ImGuiWindowFlags_None) final;

        [[nodiscard]] bool allowScroll() const final {
            return true;
        }
    };

    class View::Modal : public View {
    public:
        explicit Modal(UnlocalizedString unlocalizedName, const char* icon)
            : View(std::move(unlocalizedName), icon) {}

        void draw(ImGuiWindowFlags extraFlags = ImGuiWindowFlags_None) final;

        [[nodiscard]] virtual bool hasCloseButton() const { return true; }
        [[nodiscard]] bool shouldStoreWindowState() const override { return false; }
    };

    class View::FullScreen : public View {
    public:
        explicit FullScreen() : View("FullScreen", "") {}

        void draw(ImGuiWindowFlags extraFlags = ImGuiWindowFlags_None) final;
    };

}
