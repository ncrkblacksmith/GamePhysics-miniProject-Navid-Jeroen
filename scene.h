#ifndef SCENE_HEADER_FILE
#define SCENE_HEADER_FILE

#include <vector>
#include <fstream>
#include <igl/bounding_box.h>
#include <igl/readOFF.h>
#include "constraints.h"
#include "auxfunctions.h"
#include <igl/per_vertex_normals.h>
#include "volInt.h"

using namespace Eigen;
using namespace std;



//the class the contains each individual rigid objects and their functionality
class Mesh{
public:
    
    //geometry
    MatrixXd origX;   //original particle positions - never change this!
    MatrixXd prevX;   //the previous time step positions
    MatrixXd currX;   //current particle positions
    MatrixXi T;       //the triangles of the original mesh
    MatrixXd currNormals;
    VectorXd invMasses;   //inverse masses of particles, computed (autmatically) as 1.0/(density * particle area)
    VectorXd radii;      //radii of particles
    int rawOffset;  //the raw index offset of the x value of V from the beginning of the 3*|V| particles
    
    
    //kinematics
    bool isFixed;  //is the object immobile
    double rigidity;  //how much the mesh is really rigid
    MatrixXd currVel;     //velocities per particle. Exactly in the size of origV
    vector<Constraint> meshConstraints;  //the systematic constraints in the mesh (i.e., rigidity)
    MatrixXd currImpulses;
    

    //Quick-reject checking collision between mesh bounding boxes.
    //Does not need updating
    
    bool isBoxCollide(const Mesh& m2){
        RowVector3d XMin1=currX.colwise().minCoeff();
        RowVector3d XMax1=currX.colwise().maxCoeff();
        RowVector3d XMin2=m2.currX.colwise().minCoeff();
        RowVector3d XMax2=m2.currX.colwise().maxCoeff();
        
        //checking all axes for non-intersection of the dimensional interval
        for (int i=0;i<3;i++)
            if ((XMax1(i)<XMin2(i))||(XMax2(i)<XMin1(i)))
                return false;
        
        return true;  //all dimensional intervals are overlapping = intersection
        
    }

    
    //this function creates all collision constraints between two particle meshes
    void CreateCollisionConstraint(const Mesh& m, vector<Constraint>& collConstraints){
        
        
        //collision between bounding boxes
        if (!isBoxCollide(m))
            return;
        
        //checking collision between every two particles
        //This assumes that prevX are not intersecting. That could potentially cause artifacts
        for (int i=0;i<currX.rows();i++){
            for (int j=0;j<m.currX.rows();j++){
                if ((currX.row(i)-m.currX.row(j)).norm()>radii(i)+m.radii(j))  //mutual distance longer than sum of radii
                    continue;  //no collision
                
                //cout<<"collision between particle at "<<currX.row(i)<<" with radius "<<radii(i)<<" and particle at "<<m.currX.row(j)<<" with radius "<<m.radii(j)<<endl;
                
                //naive constraint
                VectorXi particleIndices(6); particleIndices<<rawOffset+3*i, rawOffset+3*i+1, rawOffset+3*i+2, m.rawOffset+3*j, m.rawOffset+3*j+1, m.rawOffset+3*j+2;
                VectorXd rawInvMasses(6);       rawInvMasses<<invMasses(i), invMasses(i),invMasses(i),m.invMasses(j),m.invMasses(j),m.invMasses(j);
                VectorXd rawRadii(6);           rawRadii <<radii(i), radii(i), radii(i), m.radii(j), m.radii(j), m.radii(j);
                Constraint c(COLLISION,  particleIndices, rawRadii,rawInvMasses, 0.0, 1.0);
                collConstraints.push_back(c);
            }
        }
    }
    
    //Updating the velocities currVel of the particles, from currImpulses and the external forces
    //You need to modify this to integrate from acceleration in the field (basically gravity)
    void integrateVelocity(double timeStep){
        
        if (isFixed)
            return;
        
        /***************
         TODO
         ***************/
        currImpulses.setZero();
    }
    

    //Update the current position currX
    void integratePosition(double timeStep){
        //just forward Euler now
        if (isFixed)
            return;  //a fixed object is immobile
        
        /***************
         TODO
         ***************/
        
        igl::per_vertex_normals(currX, T, currNormals);
    }
    
    
    //updating the current velocities to match the positional changes
    void projectVelocities(double timeStep){
        /***************
         TODO
         ***************/
        prevX=currX;
    }
    

    //the full integration for the time step (velocity + position)
    void integrate(double timeStep){
        integrateVelocity(timeStep);
        integratePosition(timeStep);
    }
    
    
    Mesh(const MatrixXd& _X, const MatrixXi& _T, const int _rawOffset, const double density, const double _rigidity, const bool _isFixed, const RowVector3d& userCOM, const RowVector4d& userOrientation){
        origX=_X;
        T=_T;
        isFixed=_isFixed;
        rawOffset=_rawOffset;
        rigidity=_rigidity;
        currVel=MatrixXd::Zero(origX.rows(),3);
        currImpulses=MatrixXd::Zero(origX.rows(),3);
  
		RowVector3d naturalCOM;  //by the geometry of the object
		Matrix3d invIT; //it is not used in this practical
		double mass; //as well

        //initializes the origianl geometry (COM + IT) of the object. the invIT is a stub here (bu you may want to use it).
		getCOMandInvIT(origX, T, density, mass, naturalCOM, invIT);

		origX.rowwise() -= naturalCOM;  //removing the natural COM of the OFF file (natural COM is never used again)
    
        //applying the user-prescribed COM+orientation
        currX.resize(origX.rows(), 3);
        for (int i=0;i<currX.rows();i++)
            currX.row(i)<<QRot(origX.row(i), userOrientation)+userCOM;
        
        prevX=currX;
        
        //dynamics initialization
        //the masses are set to the area of the third of each triangle adjacent to each vertex,summed up for the vertex, and multiplied with density
        VectorXd A;
        igl::doublearea(currX,T,A);
        VectorXd massV=VectorXd::Zero(currX.rows());
        for (int i=0;i<T.rows();i++){
            for (int j=0;j<3;j++)
                massV(T(i,j))+=A(i);
        }
        
        massV*=density/3.0;
        invMasses=1.0/massV.array();
        
        //radii are set so every vertex coveres at least half of every adjacent edge
        radii=VectorXd::Zero(currX.rows());
        for (int i=0;i<T.rows();i++){
            for (int j=0;j<3;j++){
                double edgeLength=(currX.row(T(i,j))-currX.row(T(i,(j+1)%3))).norm();
                radii(T(i,j))=std::max(radii(T(i,j)), edgeLength/2.0);
                radii(T(i,(j+1)%3))=std::max(radii(T(i,(j+1)%3)), edgeLength/2.0);
            }
        }
        
        
        igl::per_vertex_normals(currX, T, currNormals);
        
        
        //create mesh rigidity constraints. Note the usage of the raw indexing
		for (int i=0; i<T.rows();i++)
			for (int j=0; j<3; j++) {
				VectorXi particleIndices(6); particleIndices << 3*T(i,j), 3*T(i,j)+1, 3*T(i,j)+2, 3*T(i,(j+1)%3), 3*T(i,(j+1)%3)+1, 3*T(i,(j+1)%3)+2;
                particleIndices.array()+=rawOffset;
				VectorXd rawRadii(6); rawRadii << radii(T(i, j)), radii(T(i, j)), radii(T(i, j)), radii(T(i, (j + 1) % 3)), radii(T(i, (j + 1) % 3)), radii(T(i, (j + 1) % 3));
				VectorXd rawInvMasses(6); rawInvMasses << invMasses(T(i, j)), invMasses(T(i, j)), invMasses(T(i, j)), invMasses(T(i, (j + 1) % 3)), invMasses(T(i, (j + 1) % 3)), invMasses(T(i, (j + 1) % 3));
				double edgeLength = (currX.row(T(i, j)) - currX.row(T(i, (j + 1) % 3))).norm();
				meshConstraints.push_back(Constraint(RIGIDITY, particleIndices, rawRadii, rawInvMasses, edgeLength, 1.0));

			}
    }
    
    ~Mesh(){}
};



//This class contains the entire scene operations, and the engine time loop.
class Scene{
public:
    double currTime;
    
    VectorXd rawX;
    VectorXd rawVel;
    VectorXd rawImpulses;
    MatrixXi T;
    
    vector<Mesh> meshes;
    double platWidth, platHeight;  //used to create the platform constraint
    
    vector<Constraint> interMeshConstraints;   //constraints between meshes (mostly attachments read from the file
    
    
    //updates from global raw indices back into mesh current positions.
    void updateMeshValues(){
        for (int i=0;i<meshes.size();i++){
            for (int j=0;j<meshes[i].currX.rows();j++){
                //cout<<"meshes[i].currX.row(j) before:"<<meshes[i].currX.row(j)<<endl;
                meshes[i].currX.row(j)<<rawX.segment(meshes[i].rawOffset+3*j,3).transpose();
                //cout<<"meshes[i].currX.row(j) after:"<<meshes[i].currX.row(j)<<endl;
                meshes[i].currVel.row(j)<<rawVel.segment(meshes[i].rawOffset+3*j,3).transpose();
                meshes[i].currImpulses.row(j)<<rawImpulses.segment(meshes[i].rawOffset+3*j,3).transpose();
            }
        }
    }
    
    //update from mesh current positions into global raw indices.
    void updateRawValues(){
        for (int i=0;i<meshes.size();i++){
            for (int j=0;j<meshes[i].currX.rows();j++){
                rawX.segment(meshes[i].rawOffset+3*j,3)<<meshes[i].currX.row(j).transpose();
                rawVel.segment(meshes[i].rawOffset+3*j,3)<<meshes[i].currVel.row(j).transpose();
                rawImpulses.segment(meshes[i].rawOffset+3*j,3)<<meshes[i].currImpulses.row(j).transpose();
            }
        }
    
    }
    
    
    //adding an objects. You do not need to update this generally
    void addMesh(const MatrixXd& meshX, const MatrixXi& meshT, const double density, const double rigidity, const bool isFixed, const RowVector3d& COM, const RowVector4d orientation){
        
        Mesh m(meshX,meshT, rawX.size(), density, rigidity, isFixed, COM, orientation);
        meshes.push_back(m);
        int oldTsize=T.rows();
        T.conservativeResize(T.rows()+meshT.rows(),3);
        T.block(oldTsize,0,meshT.rows(),3)=meshT.array()+rawX.size()/3;  //to offset T to global index
        rawX.conservativeResize(rawX.size()+meshX.size());
        rawVel.conservativeResize(rawVel.size()+meshX.size());
        rawImpulses.conservativeResize(rawImpulses.size()+meshX.size());
        updateRawValues();
        
        //cout<<"rawVel: "<<rawVel<<endl;
    }
    
    
    /*********************************************************************
    This function handles a single position-based time step
     1. Integrating velocities, and position
     2. detecting collisions and generating constraints
     3. Resolving constraints iteratively until the system is valid
     4. updating velocities to match positions
     *********************************************************************/
    void updateScene(double timeStep, double CRCoeff, MatrixXd& fullX, MatrixXi& fullT, const double tolerance, const int maxIterations, const MatrixXd& platX, const MatrixXi& platT){
        
        //1. integrating velocity, position and orientation from forces and previous states
        for (int i=0;i<meshes.size();i++)
            meshes[i].integrate(timeStep);
        
        updateRawValues();
        //cout<<"raw positions: "<<rawV<<endl;
        
        vector<Constraint> fullConstraints;
        vector<Constraint> collConstraints;

        //2. detecting collisions and generating constraints the are aggragated in collConstraints
        for (int i=0;i<meshes.size();i++)
			for (int j = i + 1; j < meshes.size(); j++) {
				meshes[i].CreateCollisionConstraint(meshes[j], collConstraints);
			}
        
        //creating platform-collision constraints for y<platHeight when either x or z are between [-platWicth, platWidth]

        /***************
         TODO
         ***************/
    
        //aggregating mesh and inter-mesh constraints
        
        /***************
         TODO
         ***************/
        
        
        //3. Resolving constraints iteratively until the system is valid (all constraints are below "tolerance" , or passed maxIteration*fullConstraints.size() iterations
        //add proper impulses to rawImpulses for the corrections
        /***************
        TODO
        ***************/
      
        
        //4. updating velocities to match positions (the position-based step)
        updateMeshValues();
        if (timeStep>tolerance)  //just to allow initialization with t=0.0 in the beginning of the run.
            for (int i=0;i<meshes.size();i++)
                meshes[i].projectVelocities(timeStep);
        
        
        updateRawValues();
    
        //Updating visualization variables
        currTime+=timeStep;
        fullX.conservativeResize(rawX.size()/3+platX.rows(),3);
        for (int i=0;i<rawX.size()/3;i++)
            fullX.row(i)=rawX.segment(3*i,3).transpose();
        
        fullX.block(rawX.size()/3, 0, platX.rows(), 3)=platX;

        fullT.conservativeResize(T.rows()+platT.rows(),3);
        fullT<<T, platT.array()+ rawX.size() / 3;
    }
    
    //loading a scene from the scene .txt files
    //you do not need to update this function
    bool loadScene(const std::string dataFolder, const std::string sceneFileName, const double _platWidth, const double _platHeight, VectorXi& attachM1, VectorXi& attachV1, VectorXi& attachM2, VectorXi& attachV2){

        platWidth=_platWidth;
        platHeight=_platHeight;
        ifstream sceneFileHandle;
        sceneFileHandle.open(dataFolder+std::string("/")+sceneFileName);
        if (!sceneFileHandle.is_open())
            return false;
        int numofObjects, numofConstraints;
        
        currTime=0;
        sceneFileHandle>>numofObjects>>numofConstraints;
        for (int i=0;i<numofObjects;i++){
            MatrixXi objT;
            MatrixXd objX;
            std::string OFFFileName;
            bool isFixed;
            double density, rigidity;
            RowVector3d COM;
            RowVector4d orientation;
            sceneFileHandle>>OFFFileName>>density>>rigidity>>isFixed>>COM(0)>>COM(1)>>COM(2)>>orientation(0)>>orientation(1)>>orientation(2)>>orientation(3);
            orientation.normalize();
            igl::readOFF(dataFolder+std::string("/")+OFFFileName,objX,objT);
            addMesh(objX,objT,density, rigidity, isFixed, COM, orientation);
        }
        
        
        //reading and adding inter-mesh attachment constraints
        attachM1.resize(numofConstraints);
        attachV1.resize(numofConstraints);
        attachM2.resize(numofConstraints);
        attachV2.resize(numofConstraints);
        for (int i=0;i<numofConstraints;i++){
            sceneFileHandle>>attachM1(i)>>attachV1(i)>>attachM2(i)>>attachV2(i);
            
            for (int j=0;j<3;j++){
                VectorXi particleIndices(2); particleIndices<<meshes[attachM1(i)].rawOffset+3*attachV1(i)+j,meshes[attachM2(i)].rawOffset+3*attachV2(i)+j;
                VectorXd rawRadii(2); rawRadii<<meshes[attachM1(i)].radii(attachV1(i)), meshes[attachM2(i)].radii(attachV2(i));
                VectorXd rawInvMasses(2); rawInvMasses<<meshes[attachM1(i)].invMasses(attachV1(i)), meshes[attachM2(i)].invMasses(attachV2(i));
                double refValue=meshes[attachM1(i)].currX(attachV1(i),j)-meshes[attachM2(i)].currX(attachV2(i),j);
                interMeshConstraints.push_back(Constraint(ATTACHMENT, particleIndices, rawRadii, rawInvMasses, refValue, 1.0));
            
            }
            
            
            
        }
    
        return true;
    }
    
    
    Scene(){}
    ~Scene(){}
};




#endif