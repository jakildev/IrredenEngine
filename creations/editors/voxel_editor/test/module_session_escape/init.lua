local retained

IREditor.registerSession("escape", function(session)
    retained = session
    session:tapKey(IRInput.Key.SPACE)
end)

IREditor.registerPanel("EscapePanel", function(x, y, w, h)
    retained:sceneSize()
end)
