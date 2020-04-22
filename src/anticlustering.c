
#include <stdlib.h> 
#include <stdio.h>
#include <math.h>
#include "declarations.h"

/* Exchange Method for Anticlustering
        * param *data: vector of data points (in R, this is a data frame,
        *          the matrix structure must be restored in C)
        * param *N: The number of elements (i.e., number of "rows" in *data)
        * param *M: The number of features (i.e., number of "cols" in *data)
        * param *K: The number of clusters
        * param *frequencies: The number of elements per cluster, i.e., an array
        *          of length K.
        * param *clusters: An initial assignment of elements to clusters,
        *          array of length *N (has integers between 0 and (K-1) - this 
        *          has to be guaranteed by the caller)
        * 
        * The return value is assigned to the argument `clusters`, via pointer
*/

void c_anticlustering(
        double *data, 
        int *N, 
        int *M,
        int *K, 
        int *frequencies,
        int *clusters) {
        
        const size_t n = (size_t) *N; // number of data points
        const size_t m = (size_t) *M; // number of variables per data point
        const size_t k = (size_t) *K; // number of clusters
        
        /* 
         * 1. required data structure:
         *     Array of data points (i.e., array of struct `element`)
         */
        struct element POINTS[n];
        if (fill_data_points(data, n, m, POINTS, clusters) == 1) {
                return;
        }

        /* 
         * 2. Required data structure:
         * 
         *     K Cluster lists + one array that points to the HEAD of each 
         *     cluster list, respectively. The HEAD of each cluster list 
         *     is not a data element, but just a pointer to the first
         *     element.
         * 
         */
        
        struct node *PTR_CLUSTER_HEADS[k];
        if (initialize_cluster_heads(n, k, PTR_CLUSTER_HEADS, POINTS) == 1) {
                return; 
        }

        /* 3. Required data structure: 
         *     Array of pointers to data points (i.e., to the nodes in the 
         *     cluster lists). Used for iterating through data 
         *     points during exchange method. 
         */
        struct node *PTR_NODES[n];
        /* (a) add the i'th data point to a cluster list,
         * (b) store the pointer to its node in the cluster list */
        if (fill_cluster_lists(n, k, clusters, POINTS, PTR_NODES, 
                               PTR_CLUSTER_HEADS) == 1) {
                return;
        }

        /* 
         * 4. required data structure: (K x M) matrix of cluster centers
         */ 
        
        double CENTERS[k][m];
        for (size_t i = 0; i < k; i++) {
                compute_center(m, CENTERS[i], PTR_CLUSTER_HEADS[i], frequencies[i]);
        }
        
        /* Get variance objective of the initial cluster assignment */
        double OBJECTIVE_BY_CLUSTER[k]; 
        objective_by_cluster(m, k, OBJECTIVE_BY_CLUSTER, CENTERS, PTR_CLUSTER_HEADS);
        double SUM_VAR_OBJECTIVE = array_sum(k, OBJECTIVE_BY_CLUSTER);

        
        /* Main iteration loop for exchange procedure */
        
        /* Some variables for bookkeeping during the optimization */
        
        size_t best_partner;
        double tmp_centers[k][m];
        double best_centers[k][m];
        double tmp_objectives[k];
        double best_objectives[k];
        double tmp_objective;
        double best_objective;
        
        /* 1. Level: Iterate through `n` data points */
        for (size_t i = 0; i < n; i++) {
                size_t cl1 = PTR_NODES[i]->data->cluster;
                
                // Initialize `best` variable for the i'th item
                best_objective = 0;
                copy_matrix(k, m, CENTERS, best_centers);
                copy_array(k, OBJECTIVE_BY_CLUSTER, best_objectives);
                
                /* 2. Level: Iterate through `n` exchange partners */
                for (size_t j = 0; j < n; j++) {
                        
                        size_t cl2 = PTR_NODES[j]->data->cluster;
                        // no swapping attempt if in the same cluster:
                        if (cl1 == cl2) { 
                                continue;
                        }

                        // Initialize `tmp` variables for the exchange partner:
                        copy_matrix(k, m, CENTERS, tmp_centers);
                        copy_array(k, OBJECTIVE_BY_CLUSTER, tmp_objectives);
                        
                        update_centers(
                                k, m, 
                                tmp_centers, 
                                PTR_NODES[i],
                                PTR_NODES[j],
                                frequencies
                        );
                        swap(
                                n, i, j,
                                PTR_NODES
                        );
                        update_objective_by_cluster(
                                k, m, 
                                tmp_centers, cl1, cl2, 
                                PTR_CLUSTER_HEADS, 
                                tmp_objectives
                        );
                        tmp_objective = array_sum(k, tmp_objectives);
                        
                        // Update `best` variables if objective was improved
                        if (tmp_objective > best_objective) {
                                best_objective = tmp_objective;
                                copy_matrix(k, m, tmp_centers, best_centers);
                                copy_array(k, tmp_objectives, best_objectives);
                                best_partner = j;
                        }
                        
                        // Swap back to test next exchange partner
                        swap(
                                n, i, j,
                                PTR_NODES
                        );
                }
                
                // Only if objective is improved: Do the swap
                if (best_objective > SUM_VAR_OBJECTIVE) {
                        // Swap the current element with the best partner
                        swap(
                                n, i, best_partner,
                                PTR_NODES
                        );

                        // Update the "global" variables
                        SUM_VAR_OBJECTIVE = best_objective;
                        copy_matrix(k, m, best_centers, CENTERS);
                        copy_array(k, best_objectives, OBJECTIVE_BY_CLUSTER);
                }
        }

        // Write output
        for (size_t i = 0; i < n; i++) {
                clusters[i] = PTR_NODES[i]->data->cluster;
        }
        
        // in the end, free allocated memory:
        free_points(n, POINTS);
        free_nodes(k, PTR_CLUSTER_HEADS);
}

/* 
 * Perform a swap between two elements.
 * 
 * param `size_t n`: Number of data points
 * param `size_t i`: Index of first element
 * param `size_t j`: Index of second element
 * param `struct node *PTR_NODES[n]`: pointer to nodes
 * 
 */

void swap(size_t n, size_t i, size_t j, 
          struct node *PTR_NODES[n]) {
        
        struct node *one = PTR_NODES[i];
        struct node *two = PTR_NODES[j];
        
        // Get cluster indices
        size_t cl1 = one->data->cluster;
        size_t cl2 = two->data->cluster;
        
        // Update pointer in `PTR_NODES`
        size_t ID1 = one->data->ID;
        size_t ID2 = two->data->ID;
        PTR_NODES[ID1] = two;
        PTR_NODES[ID2] = one;
        
        // Update ID of the elements
        one->data->ID = ID2;
        one->data->ID = ID1;
        
        // Update the cluster affiliation
        one->data->cluster = cl2;
        two->data->cluster = cl1;
        
        // Update nodes in cluster lists 
        struct element *tmp = one->data;
        one->data = two->data;
        two->data = tmp;
        
}

/* Update cluster centers after a swap
 * 
 * param `size_t k`: Number of clusters
 * param `size_t m`: Number of variables per data point
 * param `double centers[k][m]`: Matrix of cluster centers
 * param `struct node *head_array[k]`: Array of HEADs to the cluster lists
 * param `*frequencies`: Array of length k, number of elements per cluster
 * 
 */
void update_centers(size_t k, size_t m, double CENTERS[k][m],
                    struct node *one, struct node *two,
                    int *frequencies) {
        // update cluster centers
        size_t cl1 = one->data->cluster;
        size_t cl2 = two->data->cluster;
        for (int i = 0; i < m; i++) {
          double change_cl1 = one->data->values[i] / frequencies[cl1];
          double change_cl2 = two->data->values[i] / frequencies[cl2];
          // Update first cluster center
          CENTERS[cl1][i] = CENTERS[cl1][i] + change_cl2;
          CENTERS[cl1][i] = CENTERS[cl1][i] - change_cl1;
          // Update second cluster center
          CENTERS[cl2][i] = CENTERS[cl2][i] - change_cl2;
          CENTERS[cl2][i] = CENTERS[cl2][i] + change_cl1;
        }
}

/* Update the objective after the swap of two element
 *
 * param `size_t k`: Number of clusters
 * param `size_t m`: Number of variables per data point
 * param `double CENTERS[k][m]`: Matrix of cluster centers
 * param `size_t cl1`: Index of the first cluster
 * param `size_t cl2`: Index of the second cluster
 * param `struct node *head_array[k]`: Array of HEADs to the cluster lists
 * param `double OBJECTIVE_BY_CLUSTER[k]`: objective by cluster
 * 
 */
void update_objective_by_cluster(size_t k, size_t m, double CENTERS[k][m], size_t cl1, 
                        size_t cl2, struct node *head_array[k], 
                        double OBJECTIVE_BY_CLUSTER[k]) {
        OBJECTIVE_BY_CLUSTER[cl1] = cluster_variance(m, head_array[cl1], CENTERS[cl1]);
        OBJECTIVE_BY_CLUSTER[cl2] = cluster_variance(m, head_array[cl2], CENTERS[cl2]);
}



/* Compute variance for a cluster
 * param `size_t m`: Number of variables per data point
 * param `struct node *ptr_to_cluster`: Pointer to a cluster list HEAD
 * param `double center[m]`: Array of mean feature values in the cluster
 */
double cluster_variance(
                size_t m, 
                struct node *HEAD, 
                double center[m]) {
        
        double sum = 0;
        struct node *tmp = HEAD->next;
        while (tmp != NULL) {
                sum += euclidean_squared(
                        center, 
                        tmp->data->values, 
                        m
                );
                tmp = tmp->next;
        }
        return sum;
}

/* Compute cluster center for one cluster
 * param `size_t m`: Number of variables per data point
 * param `double center[m]`: Empty array of cluster centers
 * param `struct node *ptr_to_cluster`: Pointer to a cluster list HEAD
 * param `int frequency`: Number of elements in the cluster
 * 
 * The input array `center` is changed through this function to represent
 * one cluster center.
 * 
 */
void compute_center(size_t m, 
                double center[m], 
                struct node *HEAD, 
                int frequency) {
        // Initialize center matrix as 0:
        for (size_t i = 0; i < m; i++) {
                center[i] = 0; 
        }

        struct node *tmp = HEAD->next; // start one next to HEAD, there is first element
        while (tmp != NULL) {
                for (size_t i = 0; i < m; i++) {
                        center[i] = center[i] + tmp->data->values[i];
                }
                tmp = tmp->next;
        } 
        // To get cluster centers: Divide by number of elements 
        for (size_t i = 0; i < m; i++) {
                center[i] = center[i] / frequency;
        }
}

/* Append data point to linked list
 * 
 * param `struct node *ptr_to_cluster` Pointer to start of linked list, representing a cluster
 * param `double *x`: Array / pointer to M data points
 * param `size_t i`: Index of the element in the original data
 * 
 * return: Pointer to the `node` of the element that was appended to the cluster list
 * 
 * side effect: Element is appended to linked list. As soon as the second element
 *      is appended to a list, the list becomes circular (last element points to 
 *      first element)
 * 
 */

struct node* append_element_to_cluster(
        struct node *HEAD, 
        struct element *data, size_t i) {

        // Append new element at the beginning of the list. Temporarily hold 
        // NEXT element of HEAD, which the becomes NEXT element of the new element.

        // Case 1: Is list empty so far? It is if HEAD->next is NULL
        if (HEAD->next == NULL) {
                HEAD->next = malloc(sizeof(struct node*));
                if (HEAD->next == NULL) {
                        return NULL;
                }
                // New element is right next to HEAD element:
                HEAD->next->data = data;
                HEAD->next->next = NULL; 
                return HEAD->next;
        }
        // Case 2: List is not empty; new node is appended next to HEAD
        struct node *tmp = HEAD->next; 
        HEAD->next = malloc(sizeof(struct node*));
        if (HEAD->next == NULL) {       
                return NULL;
        }
        // New element is right next to HEAD element:
        HEAD->next->data = data;
        HEAD->next->next = tmp; // tmp was next to HEAD before
        return HEAD->next; 
}

/* Print a cluster list 
 * 
 * param struct node *cluster: pointer to cluster list 
 * param size_t M: The number of values per element
 * 
 */
void print_cluster(struct node *HEAD, size_t M) {

        if (HEAD->next == NULL) {
                printf("Warning: Cluster list should be printed, but was empty.");
                return;
        }
        
        struct node *tmp = HEAD->next;

        int j = 1;
        
        while (tmp != NULL) {
                printf("%d: ", j);
                for (size_t i = 0; i < M; i++) {
                        printf("%10g, ", tmp->data->values[i]);
                }
                printf("\n");
                if (tmp->next == NULL) {
                        break;
                }
                tmp = tmp->next;
                j++;
        }  // iterate until we are back at the HEAD
}

/* Extracted method that fill data points into array of struct `element`
 * param `double *data` pointer to original data array of length n
 * param `size_t m`: Number of data points
 * param `size_t m`: Number of variables per data point
 * param `struct element points[n]`: Array to be filled with data points
 * param `int *clusters`: Cluster affiliation of the n data points
 * 
 * return: `0` if all data points were successfully stored; `1` if not.
 * 
 */
int fill_data_points(
                double *data, 
                size_t n, 
                size_t m, 
                struct element POINTS[n], 
                int *clusters) {
        // Create offset variable to correctly read out data points from vector
        int m_ptr[m];
        for (size_t i = 0; i < m; i++) {
                m_ptr[i] = i * n;
        }
        
        for (size_t i = 0; i < n; i++) {
                POINTS[i].cluster = clusters[i];
                POINTS[i].ID = i;
                // allocate memory for `m` data points
                POINTS[i].values = malloc(m * sizeof(POINTS[i].values[0]));
                if (POINTS[i].values == NULL) {
                        free_points(n, POINTS);
                        print_memory_error();
                        return 1;
                } 
                for (size_t j = 0; j < m; j++) {
                        POINTS[i].values[j] = data[m_ptr[j]++];
                }
        }
        return 0;
}

/* Squared Euclidean Distance between two arrays of same length
 * 
 * param *x: Array / pointer to first element
 * param *y: Array / pointer to second element
 * param m: length of the two arrays
 * 
 * return: The squared euclidean distance
 * 
 */

double euclidean_squared(double *x, double *y, size_t m) {
        double sum = 0;
        for (size_t i = 0; i < m; i++) {
                sum = sum + pow(x[i] - y[i], 2);
        }
        return sum;
}

/* Copy one array into another */
void copy_array(size_t n, double origin[n], double target[n]) {
        for (int i = 0; i < n; i++) {
                target[i] = origin[i];
        }
}

/* Copy one matrix into another */
void copy_matrix(size_t n, size_t m, double origin[n][m], double target[n][m]) {
        for (int i = 0; i < n; i++) {
                for (int j = 0; j < m; j++) {
                        target[i][j] = origin[i][j];
                }
        }
}

/* Print out data points from nodes in original order (not from the cluster lists) */
void print_elements(size_t n, size_t m, struct node **PTR_NODES) {
        for (size_t i = 0; i < n; i++) {
                printf("%zu: ", i);
                for (size_t j = 0; j < m; j++) {
                        printf("%10g, ", PTR_NODES[i]->data->values[j]);
                }
                printf("%10zu \n", PTR_NODES[i]->data->cluster);
        }
}

/* After creation, initialize the HEAD of each cluster list
 * 
 * We access the cluster list using an array of length `k` pointing
 * to the `k` HEADs of the clusters. The first element pointed to 
 * in the list (i.e., the HEAD) is "empty". It just points to the 
 * first real element that is in that cluster.
 * 
 * param `size_t k`: The number of clusters
 * param `struct node *PTR_CLUSTER_HEADS[k]`: The array of pointers to 
 *     cluster HEADS.
 * 
 *  * return: `0` if the cluster list could be initialized successfully, `1` 
 *      if not (in that case, there was no memory that could be allocated).
 * 
 */

int initialize_cluster_heads(
        size_t n, size_t k, 
        struct node *PTR_CLUSTER_HEADS[k],
        struct element POINTS[n]) {
        for (size_t i = 0; i < k; i++) {
                PTR_CLUSTER_HEADS[i] = malloc(sizeof(struct node*));
                if (PTR_CLUSTER_HEADS[i] == NULL) {
                        free_points(n, POINTS);
                        free_nodes(k, PTR_CLUSTER_HEADS);
                        print_memory_error();
                        return 1;
                }
                PTR_CLUSTER_HEADS[i]->next = NULL;
                PTR_CLUSTER_HEADS[i]->data = NULL;
        }
        return 0;
}

/* After initialization, fill the cluster lists with data */
int fill_cluster_lists(
        size_t n, 
        size_t k,
        int *clusters,
        struct element POINTS[n],
        struct node *PTR_NODES[n], 
        struct node *PTR_CLUSTER_HEADS[k]) {
        for (size_t i = 0; i < n; i++) {
                PTR_NODES[i] = append_element_to_cluster(
                        PTR_CLUSTER_HEADS[clusters[i]], 
                        &POINTS[i], i
                );
                if (PTR_NODES[i] == NULL) { // failed to allocate memory
                        free_points(n, POINTS);
                        free_nodes(k, PTR_CLUSTER_HEADS);
                        print_memory_error();
                        return 1; 
                }
        }
        return 0;
}

/* Compute sum of squared errors between cluster center and data points (i.e.
 * variance) for each of the k clusters
 */
void objective_by_cluster(
        size_t m, size_t k, 
        double VAR_OBJECTIVE[k], 
        double CENTERS[k][m], 
        struct node *PTR_CLUSTER_HEADS[k]) {
        for (size_t i = 0; i < k; i++) {
                VAR_OBJECTIVE[i] = cluster_variance(
                        m, 
                        PTR_CLUSTER_HEADS[i], 
                        CENTERS[i]
                );
        }
}
/* Compute the sum of an array */
double array_sum(size_t k, double ARRAY[k]) {
        double sum = 0;
        for (size_t i = 0; i < k; i++) {
                sum += ARRAY[i];
        }
        return sum;
}

/* Free memory in the cluster lists
* param `size_t k`: The number of clusters
* param `struct node *PTR_CLUSTER_HEADS[k]`: The array of pointers to 
*     cluster HEADS
*/
void free_nodes(size_t k, struct node *PTR_CLUSTER_HEADS[k]) {
        struct node *ptr;
        struct node *prev; // using temp pointer for freeing
        for (size_t i = 0; i < k; i++) {
                ptr = PTR_CLUSTER_HEADS[i];
                while (ptr->next != NULL)
                {  
                        prev = ptr;
                        ptr = ptr->next;
                        free(prev);
                }
                free(ptr);
        }
        
}

/* Free memory in the data points
 * param `size_t n`: The number of data points
 * param `struct element points[n]`: Array containing data points
 */
void free_points(size_t n, struct element POINTS[n]) {
        for (size_t i = 0; i < n; i++) {
                free(POINTS[i].values);
        }
}

void print_memory_error() {
        fprintf(stderr, "Failed to allocate enough memory.");
}
