#include "workflow/image_workflow_nodes.hpp"

namespace imziv::workflow {

    void registerImageWorkflowNodes() {
        static bool registered = false;
        if (registered)
            return;
        registered = true;

        registerInputNodes();
        registerGeometryNodes();
        registerColorNodes();
        registerColorAdjustNodes();
        registerThresholdNodes();
        registerFilterNodes();
        registerEdgeNodes();
        registerAnalysisNodes();
        registerOutputNodes();
    }

}
