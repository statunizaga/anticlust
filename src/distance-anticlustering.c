
#include <stdlib.h> 
#include <stdio.h>
#include <math.h>
#include "declarations.h"

/* Exchange Method for Anticlustering Based on a Distance matrix
 * 
 * param *data: vector of data points (in R, this is a distance matrix,
 *         the matrix structure must be restored in C)
 * param *N: The number of elements (i.e., number of "rows" in *data)
 * param *K: The number of clusters
 * param *frequencies: The number of elements per cluster, i.e., an array
 *         of length *K.
 * param *clusters: An initial assignment of elements to clusters,
 *         array of length *N (has to consists of integers between 0 and (K-1) 
 *         - this has to be guaranteed by the caller)
 * int *USE_CATS A boolean value (i.e., 1/0) indicating whether categorical 
 *         constraints
 * param *C: The number of categories
 * param *CAT_frequencies: The number of elements per category, i.e., an array
 *         of length *C.
 * param *categories: An assignment of elements to categories,
 *         array of length *N (has to consists of integers between 0 and (C-1) 
 *         - this has to be guaranteed by the caller)
 * 
 * The return value is assigned to the argument `clusters`, via pointer
*/

void distance_anticlustering(double *data, int *N, int *K, 
                             int *frequencies, int *clusters, 
                             int *USE_CATS, int *C, int *CAT_frequencies,
                             int *categories) {
        
        const size_t n = (size_t) *N; // number of data points
        const size_t k = (size_t) *K; // number of clusters
        
        // Per data point, store ID, cluster, and category
        struct element POINTS[n]; 
        
        for (size_t i = 0; i < n; i++) {
                POINTS[i].ID = i;
                POINTS[i].cluster = clusters[i];
                POINTS[i].values = malloc(sizeof(double)); // this is just a dummy malloc
                if (*USE_CATS) {
                        POINTS[i].category = categories[i];
                } else {
                        POINTS[i].category = 0;
                }
        }
        
        // Deal with categorical restrictions
        size_t c;
        if (*USE_CATS) {
                c = (size_t) *C; // number of categories 
        } else {
                c = 1;
                *CAT_frequencies = n;
        }
        size_t *C_HEADS[c];
        if (write_cheads(n, c, C_HEADS, USE_CATS, categories, 
                         CAT_frequencies, POINTS) == 1) {
                print_memory_error();
                free_points(n, POINTS, n);
                return; 
        }
        
        // Set up array of pointer-to-cluster-heads, return if memory runs out
        struct node *HEADS[k];
        if (initialize_cluster_heads(k, HEADS) == 1) {
                free_points(n, POINTS, n);
                return; 
        }

        // Set up array of pointers-to-nodes, return if memory runs out
        struct node *PTR_NODES[n];
        if (fill_cluster_lists(n, k, clusters, POINTS, PTR_NODES, HEADS) == 1) {
                return;
        }
        
        // Restore distance matrix
        
        int offsets[n]; // index variable for indexing correct cols in data matrix
        // Allocate memory for distance matrix in C
        double *distances[n];
        for (size_t i = 0; i < n; i++) {
                distances[i] = (double*) malloc(sizeof(double) * n);
                if (distances[i] == NULL) {
                        print_memory_error();
                        return; // TODO: Free stuff
                }
        }
        
        // Column offsets (to convert one-dimensional array to Row/Col major)
        for(int i = 0; i < n; i++) {
                offsets[i] = i * n;
        }
        
        // Reconstruct the data points as N x N distance matrix
        for (int i = 0; i < n; i++) {
                for (int j = 0; j < n; j++) {
                        distances[i][j] = data[offsets[j]++];
                }
        }
        
        // Initialize objective
        double OBJ_BY_CLUSTER[k];
        distance_objective(n, k, distances, OBJ_BY_CLUSTER, HEADS);
        double SUM_OBJECTIVE = array_sum(k, OBJ_BY_CLUSTER);

        /* Some variables for bookkeeping during the optimization */
        
        size_t best_partner;
        double tmp_objs[k];
        double best_objs[k];
        double tmp_obj;
        double best_obj;
        
        /* Start main iteration loop for exchange procedure */
        
        /* 1. Level: Iterate through `n` data points */
        for (size_t i = 0; i < n; i++) {
                size_t cl1 = PTR_NODES[i]->data->cluster;
                
                // Initialize `best` variable for the i'th item
                best_obj = 0;
                copy_array(k, OBJ_BY_CLUSTER, best_objs);
                
                /* 2. Level: Iterate through the exchange partners */
                size_t category_i = PTR_NODES[i]->data->category;
                // `category_i = 0` if `USE_CATS == FALSE`.
                size_t n_partners = CAT_frequencies[category_i];
                // `CAT_frequencies[0] == n` if `USE_CATS == FALSE`
                for (size_t u = 0; u < n_partners; u++) {
                        // recode exchange partner index
                        size_t j = C_HEADS[category_i][u];
                        size_t cl2 = PTR_NODES[j]->data->cluster;
                        // no swapping attempt if in the same cluster:
                        if (cl1 == cl2) { 
                                continue;
                        }
                        
                        // Initialize `tmp` variable for the exchange partner:
                        copy_array(k, OBJ_BY_CLUSTER, tmp_objs);
                        
                        // Update objective
                        // Cluster 1: Loses distances to element i
                        tmp_objs[cl1] -= distances_one_element(
                                n, distances, 
                                HEADS[cl1],
                                i
                        );
                        // Cluster 2: Loses distances to element j
                        tmp_objs[cl2] -= distances_one_element(
                                n, distances, 
                                HEADS[cl2],
                                j
                        );
                        // Now swap the elements in the cluster list
                        swap(n, i, j, PTR_NODES);
                        // Cluster 1: Gains distances to element j
                        // (here, element i is no longer in cluster 1, so
                        // we do not accidentally include d_ij, and the 
                        // self distance d_jj is 0)
                        tmp_objs[cl1] += distances_one_element(
                                n, distances, 
                                HEADS[cl1],
                                j
                        );
                        // Cluster 2: Gains distances to element 1
                        tmp_objs[cl2] += distances_one_element(
                                n, distances, 
                                HEADS[cl2],
                                i
                        );

                        tmp_obj = array_sum(k, tmp_objs);
                        
                        // Update `best` variables if objective was improved
                        if (tmp_obj > best_obj) {
                                best_obj = tmp_obj;
                                copy_array(k, tmp_objs, best_objs);
                                best_partner = j;
                        }
                        // Swap back to test next exchange partner
                        swap(n, i, j, PTR_NODES);
                }
                
                // Only if objective is improved: Do the swap
                if (best_obj > SUM_OBJECTIVE) {
                        swap(n, i, best_partner, PTR_NODES);
                        // Update the "global" variables
                        SUM_OBJECTIVE = best_obj;
                        copy_array(k, best_objs, OBJ_BY_CLUSTER);
                }
        }

        // Write output
        for (size_t i = 0; i < n; i++) {
                clusters[i] = PTR_NODES[i]->data->cluster;
        }
        
        // in the end, free allocated memory:
        free_points(n, POINTS, n);
        free_nodes(k, HEADS);
        // TODO: Free memory allocated for category indexes!
}

// Compute sum of distances by cluster
void distance_objective(size_t n, size_t k, double *distances[n], 
                        double OBJ_BY_CLUSTER[k], struct node *HEADS[k]) {
        for (size_t i = 0; i < k; i++) {
                OBJ_BY_CLUSTER[i] = distances_within(n, distances, HEADS[i]);
        }
}

/* Compute sum of distance within one cluster
 * param struct node *HEAD: pointer to the node in the cluster where summation starts
 * (if the summation starts at the beginning, this should be HEAD->next because
 * a cluster head is empty)
 */ 
double distances_within(size_t n, double *distances[n], struct node *HEAD) {
        double sum = 0;
        // Iterate over quadratic number of distances
        struct node *current = HEAD->next;
        while (current != NULL) {
                sum += distances_one_element(n, distances, current, current->data->ID);
                current = current->next;
        }
        return sum;
}

// compute sum of distances of one node to other nodes in the same cluster
double distances_one_element(size_t n, double *distances[n], 
                             struct node *start_node, size_t ID) {
        struct node *tmp = start_node->next;
        double sum = 0;
        while (tmp != NULL) {
                sum += distances[ID][tmp->data->ID];
                tmp = tmp->next;
        }
        return sum;
}