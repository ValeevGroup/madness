//\file projPsi.cc
//\brief Projects a time evolved wave function onto an arbitrary number of bound states
/***************************************************************************************
 * By: Nick Vence
 * This code must handled with care for the following reasons:
 * 1) It uses the Gnu Scientific Library      http://www.gnu.org/software/gsl/
 *    MADNESS should be reconfigured to include these libraries
 *    ./configure LIBS="-lgsl -lgslblas"
 * 2) Consult the note above each funciton for specific information:
 *    projectPsi: Loads wave functions from disk, projects them onto an arbitrary basis
 *    projectZdip:For perturbation calculations
 *    printBasis: For debugging purposes
 *    belkic:     Reproduces an analytic integral
 * 3) k (The wavelet order) must be the same as the projected functions: see main()
 *    12 has been the default
 ***************************************************************************************/
#include <mra/mra.h>
#include <complex>
#include <iostream>
#include <string>
#include <fstream>
using std::ofstream;
#include "wavef.h"
#include <stdlib.h>
#define PRINT(str) if(world.rank()==0) cout << str 
#define PRINTLINE(str) if(world.rank()==0) cout << str << endl

using namespace madness;

const int nIOProcessors =1;
const string prefix = "data";
typedef std::complex<double> complexd;
typedef Vector<double,NDIM> vector3D;
typedef Function<complexd,NDIM> complex_functionT;
typedef Function<double,NDIM> functionT;
typedef FunctionFactory<complexd,NDIM> complex_factoryT;
typedef FunctionFactory<double,NDIM> factoryT;
const char* wave_function_filename(int step);
bool wave_function_exists(World& world, int step);
void wave_function_store(World& world, int step, const complex_functionT& psi);
complex_functionT wave_function_load(World& world, int step);

struct WF {
    string str;
    complex_functionT func;

    WF(const string& STR, const complex_functionT& FUNC) 
        : str(STR)
        , func(FUNC) 
    {
        func.truncate();
    }
};

const char* wave_function_filename(int step) {
    static char fname[1024];
    sprintf(fname, "%s-%5.5d", prefix.c_str(), step);
    return fname;
}
bool wave_function_exists(World& world, int step) {
    return ParallelInputArchive::exists(world, wave_function_filename(step));
}
void wave_function_store(World& world, int step, const complex_functionT& psi) {
    ParallelOutputArchive ar(world, wave_function_filename(step), nIOProcessors);
    ar & psi;
}
complex_functionT wave_function_load(World& world, int step) {
    complex_functionT psi;
    ParallelInputArchive ar(world, wave_function_filename(step));
    ar & psi;
    return psi;
}

template<class T>
string toString( const T& a ) {
    ostringstream o;
    o << a[0] << ", " << a[1] << ", " << a[2];
    return o.str();
}
void loadDefaultBasis(World& world, std::vector<WF>& stateList) {
    cout << "Loading the default basis" << endl;
    const int NBSt = 3;    //Number of Bound States
    const int bSt[][3] = { {1,0,0},
                           {2,0,0},
                           {2,1,0} };
    const int NkSt = 3;    //Number of k States
    const double kSt[][3]  = { {0.0, 0.0, 0.52}
                             };
    double Z = 1.0;
    for( int i=0; i<NBSt; i++ ) {
       stateList.push_back( WF(toString(bSt[i]), 
                 FunctionFactory<complexd,NDIM>(world).functor(functorT(
                 new BoundWF(Z , bSt[i][0], bSt[i][1], bSt[i][2]) ))));
    }
    for( int i=0; i<NkSt; i++ ) {
        stateList.push_back(WF(toString(kSt[i]), 
                 FunctionFactory<complexd,NDIM>(world).functor(functorT(
                 new ScatteringWF(Z, kSt[i]) ))));
    }
    PRINTLINE("Done loading the standard basis");
}

void loadBasis(World& world, std::vector<WF>& stateList) {
    ifstream bound("bound.num");
    ifstream unbound("unbound.num");
    if( ! bound.is_open() && ! unbound.is_open() ) {
        PRINTLINE("bound.num and unbound.num not found");
        loadDefaultBasis(world,stateList);
    } else {
        double Z = 1.0;
        if(bound.is_open()) {
            PRINTLINE("Calculating bound quantum states");
            int n,l,m;
            std::ostringstream nlm;
            while(bound >> n) {
                bound >> l;
                bound >> m;
                nlm << n << l << m;
                double start = wall_time();
                PRINT(nlm.str());                
                stateList.push_back(WF(nlm.str()+"           ",
                                       FunctionFactory<complexd,NDIM>(world).
                                       functor(functorT(new BoundWF(Z,n,l,m)))));
                double used = wall_time() - start;
                PRINTLINE("\t" << used << " sec" );
                nlm.str("");
            }
            bound.close();
        } else PRINTLINE("bound.num not found");
        if(unbound.is_open()) {
            PRINTLINE("Calculating unbound quantum states");
            double kx, ky, kz;
            std::ostringstream kxyz;
            while(unbound >> kx) {
                unbound >> ky;
                unbound >> kz;
                kxyz.precision( 2 );
                kxyz << fixed;
                kxyz << kx << " " << ky << " " << kz;
                PRINT(kxyz.str());
                double start = wall_time();
                const double kvec[] = {kx, ky, kz};
                stateList.push_back(WF(kxyz.str(),
                                       FunctionFactory<complexd,NDIM>(world).
                                       functor(functorT(new ScatteringWF(Z,kvec)))));
                double used = wall_time() - start;
                PRINTLINE("\t" << used << " sec");
                kxyz.str("");
            }
            unbound.close();
        } else PRINTLINE("unbound.num not found");
    }
}
complexd zdipole( const vector3D& r) {
    return complexd(r[2],0.0);
}

/*****************************************************************
 * Dipole matrix elements available for perturbation calculations
 * |<phi_A|z|phi_B>|^2
 *****************************************************************/
void projectZdip(World& world, std::vector<WF> stateList) {
    cout.precision(8);
    complex_functionT z = complex_factoryT(world).f(zdipole);
    complexd output;
    std::vector<WF>::iterator basisI;
    std::vector<WF>::iterator basisII;
    PRINT(endl << "\t\t|<basis_m|z|basis_n>|^2 " << endl << "\t\t");
    for(basisII=stateList.begin(); basisII != stateList.end(); basisII++) {
        PRINT("|" << basisII->str << ">");
    }
    PRINT("\n");
    for(basisI = stateList.begin(); basisI !=stateList.end(); basisI++ ) {
        PRINT("<" << basisI->str << "|" );
        for(basisII = stateList.begin(); basisII != stateList.end(); basisII++) {
            output = inner(basisII->func,z*basisI->func); 
            PRINT(" " << real(output*conj(output)) <<"\t");
        }
        PRINT("\n");
    }
    PRINT("\n");
}

/****************************************************************************
 * The correlation amplitude |<Psi(+)|basis>|^2
 * wf.num                  Integer time step of the Psi(+) to be loaded
 * bound.num               Integer triplets of quantum numbers   2  1  0 
 * unbound.num             Double triplets of momentum kx ky kz  0  0  0.5
 ****************************************************************************/
void projectPsi(World& world, std::vector<WF> basisList) {
    std::vector<WF> psiList;
    ifstream f("wf.num");
    if(f.is_open()) {
        string tag;
        //LOAD Psi(+)
        while(f >> tag) {
            if(wave_function_exists(world, atoi(tag.c_str())) ) {
                psiList.push_back(WF(tag, wave_function_load(world, atoi(tag.c_str()))));
            } else {
                PRINT("Function: " << tag << " not found"<< endl);
            }
        }
        f.close();
        complex_functionT z = complex_factoryT(world).f(zdipole);
        PRINTLINE("The Psi(+) are loaded");
        PRINTLINE("\t\t|<Psi(+)|basis>|^2 ");
        complexd output;
        std::vector<WF>::iterator basisI;
        std::vector<WF>::iterator psiPlusI;
        //Display table header
        PRINT("\t\t");
        for(psiPlusI=psiList.begin(); psiPlusI != psiList.end(); psiPlusI++) {
            PRINT("|" << psiPlusI->str << ">\t\t");
        }
        //Display table row
        PRINT("\n");
        for(basisI = basisList.begin(); basisI !=basisList.end(); basisI++ ) {
            PRINT("<" << basisI->str << "|" );
            for(psiPlusI = psiList.begin(); psiPlusI != psiList.end(); psiPlusI++) {
                output = psiPlusI->func.inner(basisI->func); 
                PRINT(" " << real(output*conj(output)) << "\t");
            }
            PRINT("\n");
        }
    } else {
        PRINTLINE("File: wf.num expected to contain a " 
                  << "list of integers of loadable wave functions");
    }
}

/*************************************************
 * If you're curious about a wave function's value
 *************************************************/
void printBasis(World& world) {
    complexd output;
    double sinTH, cosTH, sinPHI, cosPHI;
    //make functions
    double dARR[3] = {0, 0, 0.5};
    vector3D kVec(dARR);
    ScatteringWF unb(1.0, kVec);
    double PHI = 0.0;
    double TH = 0.0;
    //for(double TH=0; TH<3.14; TH+=0.3 ) {

    cout.precision(2);
    for(double r=0; r<unb.domain; r+=1.0 ) {
        cout << scientific;
        cosTH =  std::cos(TH);
        sinTH =  std::sin(TH);
        cosPHI = std::cos(PHI);
        sinPHI = std::sin(PHI);
        double dARR[3] = {r*sinTH*cosPHI, r*sinTH*sinPHI, r*cosTH};        
        //        PRINTLINE(r << "\t" << unb.diffR(r) << " + I" << unb.diffI(r));
        output = unb(dARR);
        PRINTLINE(r << "\t" << real(output) << "\t" << imag(output));
    }
    //    use sed to make the complexd output standard
    //    system("sed -i '' -e's/\\+/, /' -e's/j//' f11.out");
}

/****************************************************************************
 * Reproduces atomic form integrals given by Dz Belkic's analytic expression
 ****************************************************************************/
void belkic(World& world) {
    /************************************************************************
     * qVec is the momentum transfered from the laser field
     * kVec is the momentum of the ejected electron
     * I will take these to be colinear
     ************************************************************************/
    PRINTLINE("1 0 0");
    complex_functionT b1s = complex_factoryT(world).functor(functorT( 
                                                      new BoundWF(1.0, 1, 0, 0) ));
    double dARR[3] = {0, 0, 0.5};
    const vector3D kVec(dARR);
    PRINTLINE("|" << kVec << ">");
    complex_functionT unb = complex_factoryT(world).functor(functorT(
                                                      new ScatteringWF(1.0, kVec) ));
    dARR[2] =  1.5;
    const vector3D qVec(dARR);
    PRINTLINE("Exp[I" << qVec << ".r>");
    complex_functionT expikDOTr = complex_factoryT(world).functor(functorT(
                                                      new Expikr(qVec) ));
    PRINTLINE("<k=0.5| Exp[iqVec.r] |100>");
    complexd output = inner(unb, expikDOTr*b1s);
    PRINTLINE(output);
}

void loadParameters(World& world, int& k, double& L) {
    string tag;
    ifstream f("input");
    if( f.is_open() ) {
        while(f >> tag) {
            if (tag[0] == '#') {
                char ch;
                if(world.rank() ==0) printf("    comment  %s ",tag.c_str());
                while (f.get(ch)) {
                    printf("%c",ch);
                    if (ch == '\n') break;
                }
            }
            else if (tag == "L") {
                f >> L;
                if(world.rank() == 0) printf("L = %.1f\n", L);
            }
            else if (tag == "k") {
                f >> k;
                if(world.rank() == 0) printf("k = %.1i\n",k);
            }
        }
    }
    fflush(stdout);
}

int main(int argc, char**argv) {
    // INITIALIZE the parallel programming environment
    initialize(argc, argv);
    World world(MPI::COMM_WORLD);
    PRINTLINE("After initialize");
    // Load info for MADNESS numerical routines
    startup(world,argc,argv);
    // Setup defaults for numerical functions

    PRINTLINE("Before functionDefaults after startup()");
    int k = 12;
    double L = 10.0;
    loadParameters(world, k, L);
    FunctionDefaults<NDIM>::set_k(k);               // Wavelet order
    FunctionDefaults<NDIM>::set_thresh(1e-1);       // Accuracy
    FunctionDefaults<NDIM>::set_cubic_cell(-L, L);
    FunctionDefaults<NDIM>::set_initial_level(3);
    FunctionDefaults<NDIM>::set_apply_randomize(false);
    FunctionDefaults<NDIM>::set_autorefine(false);
    FunctionDefaults<NDIM>::set_refine(true);
    FunctionDefaults<NDIM>::set_truncate_mode(0);
    FunctionDefaults<NDIM>::set_truncate_on_project(true);
    try {
        std::vector<WF> basisList;
        double dARR[3] = {0, 0, 0.5};
        vector3D rVec(dARR);
//         double start = wall_time();
//         basisList.push_back(WF("Exp(Ikr)       ",
//                                FunctionFactory<complexd,NDIM>(world).
//                                functor(functorT(new Expikr(rVec)))));
//         PRINTLINE("\tExp(Ikr)       " << wall_time()-start << " sec" 
//                   << "\t\t Size: " << basisList.back().func.size());
//         start = wall_time();
//         basisList.push_back(WF("Exp(-Ikr+kDOTr)",
//                                FunctionFactory<complexd,NDIM>(world).
//                                functor(functorT(new Expikr2(rVec)))));
//         PRINTLINE("\tExp(-Ikr+kDOTr)" << wall_time()-start << " sec"
//                   << "\t\t Size: " << basisList.back().func.size());

        loadBasis(world,basisList);
        //belkic(world);
        projectZdip(world, basisList);
        //printBasis(world);
        //projectPsi(world, basisList);         
        world.gop.fence();
        if (world.rank() == 0) {
//             world.am.print_stats();
//             world.taskq.print_stats();
//             world_mem_info()->print();
//             WorldProfile::print(world);
        }
    } catch (const MPI::Exception& e) {
        //print(e);
        error("caught an MPI exception");
    } catch (const madness::MadnessException& e) {
        print(e);
        error("caught a MADNESS exception");
    } catch (const madness::TensorException& e) {
        print(e);
        error("caught a Tensor exception");
    } catch (const char* s) {
        print(s);
        error("caught a c-string exception");
    } catch (char* s) {
        print(s);
        error("caught a c-string exception");
    } catch (const std::string& s) {
        print(s);
        error("caught a string (class) exception");
    } catch (const std::exception& e) {
        print(e.what());
        error("caught an STL exception");
    } catch (...) {
        error("caught unhandled exception");
    }
    world.gop.fence();
    ThreadPool::end();
    finalize();				//FLAG
    return 0;
}