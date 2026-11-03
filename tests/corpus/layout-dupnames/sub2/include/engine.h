#pragma once
// sub-project 2: an unrelated Engine that renders
class /*@Engine2*/Engine {
public:
    void /*@Engine2_render_decl*/render(int frame);
    int /*@Engine2_frames*/frames_ = 0;
    int /*@Engine2_id*/id_ = 2;
};
