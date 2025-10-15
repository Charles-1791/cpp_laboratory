//
// Created by Charles Green on 5/23/25.
//

#ifndef CARTESIANTREE_H
#define CARTESIANTREE_H
#include <vector>

class CartesianTree {
public:
    struct TreeNode {
        TreeNode* left_;
        TreeNode* right_;
        TreeNode* father_;
        int index_;
        TreeNode(): left_(nullptr), right_(nullptr), father_(nullptr), index_(-1) {}
        TreeNode(int index): left_(nullptr), right_(nullptr), father_(nullptr), index_(index) {}
        TreeNode(TreeNode* left, TreeNode* right, TreeNode* father, int index): left_(left), right_(right), father_(father), index_(index) {}
    };

    TreeNode* buildTree(const std::vector<int>& target) {
        TreeNode* root = new TreeNode(0);
        TreeNode* current = root;
        for(int i=1;i<target.size(); ++i) {
            TreeNode* new_node = new TreeNode(i);
            while(target[current->index_] > target[i] && current->father_) {
                current = current->father_;
            }
            if(target[current->index_] <= target[i]) {
                new_node->left_ = current->right_;
                if(current->right_) {
                    current->right_->father_ = new_node;
                }
                current->right_ = new_node;
                new_node->father_ = current;
            } else {
                current->father_ = new_node;
                new_node->left_ = current;
                root = new_node;
            }
            current = new_node;
        }
        return root;
    }

    int handleSubtree(const std::vector<int>& target, TreeNode* subroot, int base) {
        int ret = 0;
        ret += target[subroot->index_] - base;
        // update base
        if(subroot->left_) {
            ret += handleSubtree(target, subroot->left_, target[subroot->index_]);
        }
        if(subroot->right_) {
            ret += handleSubtree(target, subroot->right_, target[subroot->index_]);
        }
        return ret;
    }

};



#endif //CARTESIANTREE_H
