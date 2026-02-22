#ifndef COMPONENT_MOUSE_SCROLL_H
#define COMPONENT_MOUSE_SCROLL_H

namespace IRComponents {

struct C_MouseScroll {
    double xoffset_;
    double yoffset_;

    C_MouseScroll(double xoffset, double yoffset)
        : xoffset_(xoffset)
        , yoffset_(yoffset) {}

    C_MouseScroll()
        : xoffset_(0)
        , yoffset_(0) {}
};

} // namespace IRComponents

#endif /* COMPONENT_MOUSE_SCROLL_H */
