// Pure C raw pointer list benchmark (No dependencies, returns sum to avoid DCE)
#include <stdlib.h>

struct Node {
    int val;
    struct Node* next;
};

int main() {
    int count = 10000000; // 1000万个节点

    struct Node* head = NULL;
    for (int i = 1; i <= count; ++i) {
        struct Node* node = (struct Node*)malloc(sizeof(struct Node));
        node->val = i;
        node->next = head;
        head = node;
    }

    // 1. Sum original list
    long long sum1 = 0;
    {
        struct Node* curr = head;
        while (curr != NULL) {
            sum1 += curr->val;
            curr = curr->next;
        }
    }

    // 2. Reverse list in-place
    struct Node* prev = NULL;
    {
        struct Node* curr = head;
        while (curr != NULL) {
            struct Node* next = curr->next;
            curr->next = prev;
            prev = curr;
            curr = next;
        }
        head = prev;
    }

    // 3. Sum reversed list
    long long sum2 = 0;
    {
        struct Node* curr = head;
        while (curr != NULL) {
            sum2 += curr->val;
            curr = curr->next;
        }
    }

    // 4. Free list manually
    {
        struct Node* curr = head;
        while (curr != NULL) {
            struct Node* next = curr->next;
            free(curr);
            curr = next;
        }
    }

    // Return 0 if validation matches to avoid DCE, while keeping exit code 0
    if ((sum1 + sum2) == 100000010000000LL) {
        return 0;
    }
    return 1;
}
