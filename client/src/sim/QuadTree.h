#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

struct Entity;

// Loose quadtree. An item lives in the deepest node whose quadrant fully contains its
// bounds; a leaf splits at 5+ items while depth <= 8, and four leaf children fold back into
// their parent once they hold 4 or fewer items between them.
class QuadTree {
public:
    struct Node {
        Node* parent = nullptr;
        float x = 0, y = 0, w = 0, h = 0;
        int depth = 0;
        std::vector<Entity*> items;
        std::unique_ptr<Node[]> children; // 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right
    };

    // Rebuilds the root (bounds +-1200) only when the arena grows past every extent seen so far.
    void setArena(float left, float top, float right, float bottom);
    // Inserts the item, or re-seats it from its current node, with world bounds (x, y, w, h).
    void update(Entity* item, float x, float y, float w, float h);
    // Drops items not updated since the last prune (deleted entities), then merges.
    void prune();
    const Node& root() const { return rootNode; }

private:
    struct Placed { Node* node; float x, y, w, h; unsigned stamp; };

    void insert(Node* start, Entity* item);
    void detach(Entity* item, Node* node);
    void merge(Node& node);

    Node rootNode;
    std::unordered_map<Entity*, Placed> placed;
    unsigned stamp = 1;
    float minX = 0, minY = 0, maxX = 0, maxY = 0;
};
