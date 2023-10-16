# Entity todo implementation stuff

    -   Make components actually just entities (in the treatment of their IDs and bitsets)

    -   Implement hierarchy for components (components can have parent components)
        *   this will probably be done with some sort of bitmasking of extra id bits

    - narrow-phase spatial queries???
        *   would be useful for spatial calculations such as hit detection (storing positions, etc)
        *   Only certian componets would have to be stored this way, and would be stored in parallel
            with actual component data in archetype node

    - Runtime tagging
        *   would be useful for something like setting control groups (dota thing)
    - Clean up file names

# TODO education - always good to keep learning so I can improve implementation
    -   Make a habbit of looking at other implementations. Many found here: https://github.com/SanderMertens/ecs-faq.
        Spend at least a few hours total on each.
            * EnTT
            * Flecs https://github.com/SanderMertens/flecs 
                - Blog posts have been helpful: https://ajmmertens.medium.com/building-an-ecs-1-types-hierarchies-and-prefabs-9f07666a1e9
                - IDs with extra bits for recycling, tagging, etc
                - Archetype graph for lookup

            * AUSTIN MORLAN blog post: https://austinmorlan.com/posts/entity_component_system/
                - Very simplified implementation in C++, good starting point and reference
