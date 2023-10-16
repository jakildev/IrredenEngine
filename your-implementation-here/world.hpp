#ifndef WORLD_H
#define WORLD_H

#include <engine.hpp>

class World : public IRWorld {
public:
    World(int argc, char **argv);
    ~World();
protected:
    virtual void initGameEntities() override;
    virtual void initGameSystems() override;
};

#endif /* WORLD_H */
