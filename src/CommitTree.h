#pragma once
#include <memory>
#include <string>
#include <windows.h>
#include <algorithm>
#include <vector>
#undef max

// Relevant information stored in a commit
struct CommitInfo {
    int commitNumber;
    std::wstring fileName;
    std::wstring diffData;
    std::wstring commitMessage;
};


// Forward declerations
struct CommitNode;


// The "Mods" Stored in the Partially persistent AVL Tree
struct ModificationRecord {
    enum Field { LEFT, RIGHT, HEIGHT };
    int version;      // commit
    Field field;
    std::shared_ptr<CommitNode> newChild;
    int newHeight;

    // default constructor
    ModificationRecord()
        : version(0), field(LEFT), newChild(nullptr), newHeight(0) {
    }

    // pointer modifications
    ModificationRecord(int ver, Field f, std::shared_ptr<CommitNode> child)
        : version(ver), field(f), newChild(child), newHeight(0) {
    }

    // height modification
    ModificationRecord(int ver, Field f, int h)
        : version(ver), field(f), newChild(nullptr), newHeight(h) {
    }
};


// A commit node in the partially persistent AVL tree. Uses fat node approach from Driscoll with a fixed mod list
struct CommitNode {
    int commitCounter;
    std::wstring fileName;
    std::wstring diffData;
    std::wstring commitMessage;
    int height;
    std::shared_ptr<CommitNode> left;
    std::shared_ptr<CommitNode> right;

    // Fat node fields
    static const int MAX_MODS = 5;
    ModificationRecord mods[MAX_MODS];
    int modCount;

    CommitNode(int counter, const std::wstring& fname, const std::wstring& diff = L"", const std::wstring& msg = L"")
        : commitCounter(counter), fileName(fname), diffData(diff), commitMessage(msg),
        height(1), left(nullptr), right(nullptr), modCount(0) {
    }
};


// Return a node with most up to date fields based off mod list
std::shared_ptr<CommitNode> getLeft(const std::shared_ptr<CommitNode>& node, int version) {
    if (!node) return nullptr;
    std::shared_ptr<CommitNode> result = node->left;
    for (int i = 0; i < node->modCount; i++) {
        if (node->mods[i].field == ModificationRecord::LEFT && node->mods[i].version <= version) {
            result = node->mods[i].newChild;
        }
    }
    return result;
}


// Return a node with most up to date fields based off mod list
std::shared_ptr<CommitNode> getRight(const std::shared_ptr<CommitNode>& node, int version) {
    if (!node) return nullptr;
    std::shared_ptr<CommitNode> result = node->right;
    for (int i = 0; i < node->modCount; i++) {
        if (node->mods[i].field == ModificationRecord::RIGHT && node->mods[i].version <= version) {
            result = node->mods[i].newChild;
        }
    }
    return result;
}


// Return height of node using mod list to get most up to date information
int getHeight(const std::shared_ptr<CommitNode>& node, int version) {
    if (!node) return 0;
    int result = node->height;
    for (int i = 0; i < node->modCount; i++) {
        if (node->mods[i].field == ModificationRecord::HEIGHT && node->mods[i].version <= version) {
            result = node->mods[i].newHeight;
        }
    }
    return result;
}


// full mod list triggers a new node and leaves old node alone
std::shared_ptr<CommitNode> copyFullNode(const std::shared_ptr<CommitNode>& node, int version) {
    if (!node) return nullptr;
    auto newNode = std::make_shared<CommitNode>(node->commitCounter, node->fileName, node->diffData, node->commitMessage);
    newNode->left = getLeft(node, version);
    newNode->right = getRight(node, version);
    newNode->height = getHeight(node, version);
    newNode->modCount = 0;
    return newNode;
}


// updates the left child node, triggers a copy if mod list is full
std::shared_ptr<CommitNode> updateLeft(const std::shared_ptr<CommitNode>& node,
    const std::shared_ptr<CommitNode>& newLeft, int version) {
    if (!node) return nullptr;
    if (node->modCount < CommitNode::MAX_MODS) {
        node->mods[node->modCount] = ModificationRecord(version, ModificationRecord::LEFT, newLeft);
        node->modCount++;
        return node;
    }
    else {
        auto newNode = copyFullNode(node, version);
        newNode->left = newLeft;
        return newNode;
    }
}


// updates the right child node, triggers a copy if mod list is full
std::shared_ptr<CommitNode> updateRight(const std::shared_ptr<CommitNode>& node,
    const std::shared_ptr<CommitNode>& newRight, int version) {
    if (!node) return nullptr;
    if (node->modCount < CommitNode::MAX_MODS) {
        node->mods[node->modCount] = ModificationRecord(version, ModificationRecord::RIGHT, newRight);
        node->modCount++;
        return node;
    }
    else {
        auto newNode = copyFullNode(node, version);
        newNode->right = newRight;
        return newNode;
    }
}


// updates the height of a node, triggers a copy if mod list is full
std::shared_ptr<CommitNode> updateHeight(const std::shared_ptr<CommitNode>& node,
    int version, int newHeight) {
    if (!node) return nullptr;
    if (node->modCount < CommitNode::MAX_MODS) {
        node->mods[node->modCount] = ModificationRecord(version, ModificationRecord::HEIGHT, newHeight);
        node->modCount++;
        return node;
    }
    else {
        auto newNode = copyFullNode(node, version);
        newNode->height = newHeight;
        return newNode;
    }
}


// Perform a right rotation to rebalance tree, leaves old nodes as is and creates new nodes
std::shared_ptr<CommitNode> rightRotate(const std::shared_ptr<CommitNode>& y, int version) {
    auto x = copyFullNode(getLeft(y, version), version);
    auto T2 = getRight(x, version);
    auto newY = updateLeft(y, T2, version);
    newY = updateHeight(newY, version, 1 + std::max(getHeight(getLeft(newY, version), version),
        getHeight(getRight(newY, version), version)));
    x = updateRight(x, newY, version);
    x = updateHeight(x, version, 1 + std::max(getHeight(getLeft(x, version), version),
        getHeight(getRight(x, version), version)));
    return x;
}


// Performs a left rotation to rebalance tree
std::shared_ptr<CommitNode> leftRotate(const std::shared_ptr<CommitNode>& x, int version) {
    auto y = copyFullNode(getRight(x, version), version);
    auto T2 = getLeft(y, version);
    auto newX = updateRight(x, T2, version);
    newX = updateHeight(newX, version, 1 + std::max(getHeight(getLeft(newX, version), version),
        getHeight(getRight(newX, version), version)));
    y = updateLeft(y, newX, version);
    y = updateHeight(y, version, 1 + std::max(getHeight(getLeft(y, version), version),
        getHeight(getRight(y, version), version)));
    return y;
}


std::shared_ptr<CommitNode> insertNode(const std::shared_ptr<CommitNode>& root, int commitCounter,
    const std::wstring& fileName, const std::wstring& diffData,
    const std::wstring& commitMessage = L"") {
    int version = commitCounter;  // Each new insertion uses its commit number as its version.
    if (!root)
        return std::make_shared<CommitNode>(commitCounter, fileName, diffData, commitMessage);

    // “Copy” the root using its effective fields for the current version.
    auto newRoot = copyFullNode(root, version);
    if (commitCounter < newRoot->commitCounter) {
        auto updatedLeft = insertNode(getLeft(newRoot, version), commitCounter, fileName, diffData, commitMessage);
        newRoot = updateLeft(newRoot, updatedLeft, version);
    }
    else {
        auto updatedRight = insertNode(getRight(newRoot, version), commitCounter, fileName, diffData, commitMessage);
        newRoot = updateRight(newRoot, updatedRight, version);
    }
    int newHeight = 1 + std::max(getHeight(getLeft(newRoot, version), version), getHeight(getRight(newRoot, version), version));
    newRoot = updateHeight(newRoot, version, newHeight);

    int balance = getHeight(getLeft(newRoot, version), version) - getHeight(getRight(newRoot, version), version);

    // left left
    if (balance > 1 && commitCounter < getLeft(newRoot, version)->commitCounter)
        return rightRotate(newRoot, version);
    // right right
    if (balance < -1 && commitCounter >= getRight(newRoot, version)->commitCounter)
        return leftRotate(newRoot, version);
    // left right 
    if (balance > 1 && commitCounter >= getLeft(newRoot, version)->commitCounter) {
        auto updatedLeft = leftRotate(getLeft(newRoot, version), version);
        newRoot = updateLeft(newRoot, updatedLeft, version);
        return rightRotate(newRoot, version);
    }
    // right left 
    if (balance < -1 && commitCounter < getRight(newRoot, version)->commitCounter) {
        auto updatedRight = rightRotate(getRight(newRoot, version), version);
        newRoot = updateRight(newRoot, updatedRight, version);
        return leftRotate(newRoot, version);
    }
    return newRoot;
}


std::shared_ptr<CommitNode> searchCommit(const std::shared_ptr<CommitNode>& node, int targetCommit, int version) {
    if (!node) return nullptr;
    if (targetCommit == node->commitCounter)
        return node;
    else if (targetCommit < node->commitCounter)
        return searchCommit(getLeft(node, version), targetCommit, version);
    else
        return searchCommit(getRight(node, version), targetCommit, version);
}


std::shared_ptr<CommitNode> getSuccessor(const std::shared_ptr<CommitNode>& root, int commitNumber, int version) {
    std::shared_ptr<CommitNode> successor = nullptr;
    auto current = root;
    while (current) {
        if (commitNumber < current->commitCounter) {
            successor = current;
            current = getLeft(current, version);
        }
        else {
            current = getRight(current, version);
        }
    }
    return successor;
}


std::shared_ptr<CommitNode> getPredecessor(const std::shared_ptr<CommitNode>& root, int commitNumber, int version) {
    std::shared_ptr<CommitNode> predecessor = nullptr;
    auto current = root;
    while (current) {
        if (commitNumber > current->commitCounter) {
            predecessor = current;
            current = getRight(current, version);
        }
        else {
            current = getLeft(current, version);
        }
    }
    return predecessor;
}