#include "QuadTree.h"

#include <algorithm>

void QuadTree::setArena(float left, float top, float right, float bottom) {
    // The recorded extents start at 0 and only ever grow.
    if (!(minX > left || minY > top || maxX < right || maxY < bottom)) return;
    rootNode.items.clear();
    rootNode.children.reset();
    for (auto& [item, p] : placed) p.node = nullptr;
    rootNode.x = left - 1200;
    rootNode.y = top - 1200;
    rootNode.w = right + 1200 - rootNode.x;
    rootNode.h = bottom + 1200 - rootNode.y;
    minX = std::min(minX, left);
    minY = std::min(minY, top);
    maxX = std::max(maxX, right);
    maxY = std::max(maxY, bottom);
}

void QuadTree::update(Entity* item, float x, float y, float w, float h) {
    Placed& p = placed.try_emplace(item, Placed{nullptr, 0, 0, 0, 0, 0}).first->second;
    p.x = x; p.y = y; p.w = w; p.h = h;
    p.stamp = stamp;
    if (!p.node) { insert(&rootNode, item); return; }
    // Climb to the first ancestor-or-self that strictly contains the bounds.
    Node* n = p.node;
    while (n->parent) {
        if (x >= n->x && y >= n->y && x + w < n->x + n->w && y + h < n->y + n->h) break;
        n = n->parent;
    }
    insert(n, item);
}

void QuadTree::detach(Entity* item, Node* node) {
    auto it = std::find(node->items.begin(), node->items.end(), item);
    if (it == node->items.end()) return;
    *it = node->items.back();
    node->items.pop_back();
}

void QuadTree::insert(Node* c, Entity* item) {
    Placed& p = placed[item];
    while (c->children) {
        float midX = c->x + c->w * 0.5f, midY = c->y + c->h * 0.5f;
        int q;
        if (p.x + p.w < midX) {
            if (p.y + p.h < midY) q = 0;
            else if (p.y >= midY) q = 2;
            else break;
        } else if (p.x >= midX) {
            if (p.y + p.h < midY) q = 1;
            else if (p.y >= midY) q = 3;
            else break;
        } else {
            break;
        }
        c = &c->children[q];
    }
    if (p.node == c) return;
    if (p.node) detach(item, p.node);
    p.node = c;
    c->items.push_back(item);
    if (c->children || c->items.size() < 5 || c->depth > 8) return;

    // Split the leaf and re-insert everything it held from here.
    c->children.reset(new Node[4]);
    float hw = c->w * 0.5f, hh = c->h * 0.5f;
    for (int i = 0; i < 4; ++i) {
        Node& ch = c->children[i];
        ch.parent = c;
        ch.depth = c->depth + 1;
        ch.x = c->x + (i & 1 ? hw : 0);
        ch.y = c->y + (i & 2 ? hh : 0);
        ch.w = hw;
        ch.h = hh;
    }
    std::vector<Entity*> moved;
    moved.swap(c->items);
    for (Entity* e : moved) {
        placed[e].node = nullptr;
        insert(c, e);
    }
}

void QuadTree::merge(Node& node) {
    if (!node.children) return;
    for (int i = 0; i < 4; ++i) merge(node.children[i]);
    size_t total = node.items.size();
    for (int i = 0; i < 4; ++i) {
        if (node.children[i].children) return;
        total += node.children[i].items.size();
    }
    if (total > 4) return;
    for (int i = 0; i < 4; ++i) {
        for (Entity* e : node.children[i].items) {
            placed[e].node = &node;
            node.items.push_back(e);
        }
    }
    node.children.reset();
}

void QuadTree::prune() {
    for (auto it = placed.begin(); it != placed.end();) {
        if (it->second.stamp != stamp) {
            if (it->second.node) detach(it->first, it->second.node);
            it = placed.erase(it);
        } else {
            ++it;
        }
    }
    ++stamp;
    merge(rootNode);
}
