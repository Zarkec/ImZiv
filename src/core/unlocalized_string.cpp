#include "core/unlocalized_string.hpp"

#include <map>

namespace hex {

    std::string Lang(const UnlocalizedString& value) {
        static const std::map<std::string, std::string> translations = {
            { "imziv.view.image_workflow", "图片工作流" },
            { "imziv.view.image_editor", "图像查看器" },
            { "imziv.view.tools", "工具" },
            { "imziv.tool.measure", "测距" },
            { "imziv.tool.angle", "测角" },
            { "imziv.tool.colorpicker", "取色" },
        };

        auto iter = translations.find(value.get());
        if (iter != translations.end())
            return iter->second;

        return value.get();
    }

}
