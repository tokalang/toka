// Benchmark BST Rotate in C (Pure Computational Version)
#include <stdlib.h>

struct TreeNode {
    int val;
    struct TreeNode* left;
    struct TreeNode* right;
};

struct TreeNode* left_rotate(struct TreeNode* x) {
    if (x != NULL) {
        struct TreeNode* y = x->right;
        if (y != NULL) {
            x->right = y->left;
            y->left = x;
            return y;
        }
    }
    return x;
}

struct TreeNode* right_rotate(struct TreeNode* y) {
    if (y != NULL) {
        struct TreeNode* x = y->left;
        if (x != NULL) {
            y->left = x->right;
            x->right = y;
            return x;
        }
    }
    return y;
}

long long sum_tree(struct TreeNode* root) {
    long long sum = 0;
    if (root != NULL) {
        sum += root->val;
        sum += sum_tree(root->left);
        sum += sum_tree(root->right);
    }
    return sum;
}

void free_tree(struct TreeNode* root) {
    if (root != NULL) {
        struct TreeNode* l = root->left;
        struct TreeNode* r = root->right;
        free(root);
        free_tree(l);
        free_tree(r);
    }
}

int main() {
    int count = 200000000; // 2亿次旋转
    struct TreeNode* root = NULL;

    // Manually allocate and link tree nodes
    struct TreeNode* n5 = (struct TreeNode*)malloc(sizeof(struct TreeNode)); n5->val = 5; n5->left = NULL; n5->right = NULL;
    struct TreeNode* n3 = (struct TreeNode*)malloc(sizeof(struct TreeNode)); n3->val = 3; n3->left = NULL; n3->right = NULL;
    struct TreeNode* n8 = (struct TreeNode*)malloc(sizeof(struct TreeNode)); n8->val = 8; n8->left = NULL; n8->right = NULL;
    struct TreeNode* n2 = (struct TreeNode*)malloc(sizeof(struct TreeNode)); n2->val = 2; n2->left = NULL; n2->right = NULL;
    struct TreeNode* n4 = (struct TreeNode*)malloc(sizeof(struct TreeNode)); n4->val = 4; n4->left = NULL; n4->right = NULL;
    struct TreeNode* n7 = (struct TreeNode*)malloc(sizeof(struct TreeNode)); n7->val = 7; n7->left = NULL; n7->right = NULL;
    struct TreeNode* n9 = (struct TreeNode*)malloc(sizeof(struct TreeNode)); n9->val = 9; n9->left = NULL; n9->right = NULL;
    struct TreeNode* n1 = (struct TreeNode*)malloc(sizeof(struct TreeNode)); n1->val = 1; n1->left = NULL; n1->right = NULL;
    struct TreeNode* n6 = (struct TreeNode*)malloc(sizeof(struct TreeNode)); n6->val = 6; n6->left = NULL; n6->right = NULL;

    n5->left = n3;
    n5->right = n8;
    n3->left = n2;
    n3->right = n4;
    n8->left = n7;
    n8->right = n9;
    n2->left = n1;
    n7->left = n6;

    root = n5;

    // 1. In-place rotation stress test
    for (int i = 1; i <= count; ++i) {
        root = left_rotate(root);
        root = right_rotate(root);
    }

    // 2. Sum elements
    long long total_sum = sum_tree(root);

    // 3. Clean up
    free_tree(root);

    // Check sum (5+3+8+2+4+7+9+1+6 = 45)
    if (total_sum == 45) {
        return 0;
    }
    return 1;
}
