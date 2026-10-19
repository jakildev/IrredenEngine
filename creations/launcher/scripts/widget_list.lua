local M = {}

function M.clear(widgets)
    for _, id in ipairs(widgets) do
        IRGui.destroyWidget(id)
    end
    for index = #widgets, 1, -1 do
        widgets[index] = nil
    end
end

return M
