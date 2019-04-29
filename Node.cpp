//
// Created by Claudio Russo Introito on 2019-04-28.
//

#include <mpi.h>
#include <iostream>
#include <algorithm>
#include <vector>
#include <stddef.h>
#include <fstream>
#include <sstream>
#include <math.h>
#include "Node.h"

Node::Node(int rank, MPI_Comm comm) : rank(rank) , comm(comm) {

    //Create vector<Point> Datatype in order to be able to send and receive element of struct Punto
    int blocksize[] = {MAX_DIM, 1};
    MPI_Aint displ[] = {0, offsetof(Punto, id)};
    MPI_Datatype blockType[] = {MPI_DOUBLE, MPI_INT};

    MPI_Type_create_struct(2, blocksize, displ, blockType, &pointType);
    MPI_Type_commit(&pointType);

}

void Node::readDataset() {

    if (rank == 0) {

        // READ DATASET
        ifstream infile("twitter_points_20 copiaridotta.csv");
        string line;
        cout << "Leggo il file" << endl;

        int count = 0;
        int num = 0;
        while(getline(infile, line, '\n')){
            if (count == 0) {
                cout << "la prima riga è " << line << endl;
                stringstream ss(line);
                getline(ss, line, ';');
                total_values = stoi(line);
                cout << "Total values is: " << total_values << endl;

                getline(ss, line, ';');
                K = stoi(line);
                cout << "Number of clusters K is: " << K << endl;
                //Aggiungere qui gli ulteriori campi della prima riga (num points, num clusters, iterations) con
                // lo stesso format di total_values: quindi scrivere getline(ss, line, ';');     total_values = stoi(line);


                getline(ss, line, '\n');
                max_iterations = stoi(line);
                cout << "Max iteration is: " << max_iterations << endl;
                count++;
                //dataset.resize(total_values);
            } else {
                Punto point;
                point.id = num;
                //getline(infile, line);
                int i = 0;
                stringstream ss(line);
                while(getline(ss, line, ';')){
                    //cout << "Put value " << line << " in the array num " << point.id << endl;
                    point.values[i] = stod(line);
                    //cout << "Il valore aggiunto è: " << point.values[i] << "\n" << endl;
                    i++;
                }
                num++;
                dataset.push_back(point);

            }
        }

        infile.close();

        //cout << "First element has values : " << dataset[2].values[0] << ". Last one is " << dataset[2].values[19] << endl;

    }
}


void Node::scatterDataset() {
    /* Scatter dataset among nodes */

    int numNodes;
    MPI_Comm_size(comm, &numNodes);

    int pointsPerNode[numNodes];
    int datasetDisp[numNodes];

    if(rank == 0) {
        int numPoints = dataset.size(); //forse è di tipo unsigned long
        cout << "Total points: " << numPoints << endl;

        int partial = numPoints/numNodes;
        fill_n(pointsPerNode, numNodes, partial);

        /* Assing remainder R of the division to first R node*/
        if((numPoints % numNodes) != 0){
            int r = numPoints % numNodes;

            for(int i = 0; i < r; i ++){
                pointsPerNode[i] += 1;
            }
        }

        //Vector contains strides (https://www.mpi-forum.org/docs/mpi-1.1/mpi-11-html/node72.html) so, we need to
        // know precisely where starting to divide the several part of the vector<Punto>

        int sum = 0;
        for(int i = 0; i < numNodes; i++){
            if(i == 0){
                datasetDisp[i] = 0;
            }else{
                sum += pointsPerNode[i-1];
                datasetDisp[i] = sum;
            }
        }
    }

    MPI_Scatter(pointsPerNode, 1, MPI_INT, &num_local_points, 1, MPI_INT, 0, MPI_COMM_WORLD);

    cout << "Node " << rank << " has num of points equal to " << num_local_points << "\n" << endl;

    localDataset.resize(num_local_points);

    MPI_Scatterv(dataset.data(), pointsPerNode, datasetDisp, pointType, localDataset.data(), num_local_points, pointType, 0, MPI_COMM_WORLD);

    /*
    if(rank == 3){
        cout << "First element of Node 3 has values : " << localDataset[0].values[0] << ". Last one is " << localDataset[0].values[19] << endl;
    }
    */

    //Send the dimension of points to each node
    MPI_Bcast(&total_values, 1, MPI_INT, 0, MPI_COMM_WORLD);

    memberships.resize(num_local_points);
    for(int i = 0; i < num_local_points; i++){
        memberships[i] = -1;
    }
}



void Node::extractCluster() {

    /* To extract initially the clusters, we choose randomly K point of the dataset. This action is performed
     * by the Node 0, who sends them to other nodes in broadast. Ids of clusters are the same of their initial centroid point  */

    if(rank == 0){
        if( K >= dataset.size()){
            cout << "ERROR: Number of cluster >= number of points " << endl;
            return;
        }

        vector<int> clusterIndices;
        vector<int> prohibitedIndices;

        for(int i = 0; i < K; i++) {
            while (true) {            //TODO fix double loop
                int randIndex = rand() % dataset.size();

                if (find(prohibitedIndices.begin(), prohibitedIndices.end(), randIndex) == prohibitedIndices.end()) {
                    prohibitedIndices.push_back(randIndex);
                    clusterIndices.push_back(randIndex);
                    break;
                }
            }
        }

        //Take points which refer to clusterIndeces and send them in broadcast to all Nodes

        /* C++11 extension
        for(auto&& x: clusterIndices){      //packed range-based for loop  (https://www.quora.com/How-do-I-iterate-through-a-vector-using-for-loop-in-C++)
            clusters.push_back(dataset[x]);
        }
         */

        for(int i = 0; i < clusterIndices.size(); i++){
            clusters.push_back(dataset[clusterIndices[i]]);
        }

        /*
        cout << "The id of point chosen for initial values of cluster are : " << endl;
        for(int i = 0; i < clusters.size(); i++){
            cout << "Cluster referring to point with id: " << clusters[i].id << " with first value " << clusters[i].values[0] << endl;
        }
         */

    }

    //Send the number of clusters in broadcast
    MPI_Bcast(&K, 1, MPI_INT, 0, MPI_COMM_WORLD);

    clusters.resize(K);

    //Send the clusters centroids
    MPI_Bcast(clusters.data(), K, pointType, 0, MPI_COMM_WORLD);

    /*
    if( rank == 1){
        cout << "Node " << rank << " receive the clusters with the following values:" << endl;
        for(int i = 0; i < clusters.size(); i++){
            cout << "Node " << rank << " has cluster " << clusters[i].id << " with first value " << clusters[i].values[0] << endl;
        }
    }
     */
}

int Node::getIdNearestCluster(Punto p) {
    double sum = 0.0;
    double min_dist;
    int idCluster = 0;

    //Initialize sum and min_dist
    for(int i = 0; i < total_values; i++){
        sum += pow( clusters[0].values[i] - p.values[i], 2.0);
    }

    min_dist = sqrt(sum);
    //cout << "Point " << p.id << " is distantiated from cluster 0: " << min_dist << endl;

    //Compute the distance from others clusters
    for(int k = 1; k < K; k++){

        double dist;
        sum = 0.0;

        for(int i = 0; i < total_values; i++){
            sum += pow( clusters[k].values[i] - p.values[i], 2.0);
        }

        dist = sqrt(sum);
        //cout << "Point " << p.id << " is distantiated from cluster " << k << ": " << dist << endl;

        if(dist < min_dist){
            min_dist = dist;
            idCluster = k;
        }
    }

    return idCluster;
}


void func(Punto *in, Punto *inout, int *len, const MPI_Datatype *dptr) {
    /*params:
     *      len: corresponds to the dimension of each point (so must be equal to total_values)
     */
    int i = 0;
    //int size = in->size();
    vector<Punto> results;

    //for(int i = 0; i < size; i++){
    while(in != NULL){
        for(int j = 0; j < *len; j++){
            results[i].values[j] = in->values[j] + inout->values[j];
        }
        results[i].id = in->id;
        i++;
    }
    inout = results.data();
}

void Node::run(){

    localSum.resize(K);

    for(int i = 0; i < localDataset.size(); i++){

        int old_mem = memberships[i];
        int new_mem = getIdNearestCluster(localDataset[i]);

        if(old_mem == -1){
            memberships[i] = new_mem;
        }
        else if( new_mem < old_mem){
            memberships[i] = new_mem;
        }

        for(int j = 0; j < total_values; j++){
            localSum[memberships[i]].values[j] += localDataset[i].values[j];
        }

        cout << "In Node " << rank << " the localSum of cluster  " << clusters[i].id << " has values  0" << " equal to " << localSum[i].values[0] << endl;


    }


    /*To recalculate cluster centroids, we sum locally the points (values-to-values) which belong to a cluster.
     * The result will be a point with values equal to that sum. This point is sent (with AllReduce) to each
     * node by each node with AllReduce which computes the sum of each value-to-value among all sent points.*/

    MPI_Op myOp;
    MPI_Op_create((MPI_User_function*)func, true, &myOp);

    MPI_Allreduce(localSum.data(), localSum.data(), total_values, pointType, myOp, MPI_COMM_WORLD);     //TODO fix this


    if(rank == 0){
        for(int i = 0; i < K; i++){
            cout << "After MPI_Allreduce, In Node " << rank << " the localSum of cluster  " << clusters[i].id << " has values 0 " << " equal to " << localSum[i].values[0] << endl;

        }
    }

}







