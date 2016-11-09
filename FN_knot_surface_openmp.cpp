/**************************************************************************************//* Fitzhugh-Nagumo reaction diffusion simulation with arbitrary vortex lines    OPENMP VERSION   Created by Carl Whitfield   Last modified 8/11/16    Operational order of the code: 1) The code takes as input an stl file (defined in knot_filename) which defines an orientable surface with a boundary. 2) This surface is scaled to fill a box of size xmax x ymax x zmax. 3) A nunmerical integral is performed to calculate a phase field (phi_calc) on the 3D grid which winds around the boundary of the surface. 4) This is then used to initialise the Fitzhugh-Nagumo set of partial differential equations such that on initialisation (uv_initialise): u = 2cos(phi) - 0.4        v = sin(phi) - 0.4 The pde's used are dudt = (u - u^3/3 - v)/epsilon + Del^2 u dvdt = epsilon*(u + beta - gam v)  5) The update method is Runge-Kutta fourth order (update_uv) unless RK4 is set to 0, otherwise Euler forward method is used.   The parameters epsilon, beta and gam are set to give rise to scroll waves (see Sutcliffe, Winfree, PRE 2003 and Maucher Sutcliffe PRL 2016) which eminate from a closed curve, on initialisation this curve corresponds to the boundary of the surface in the stl file.  See below for various options to start the code from previous outputted data.*/ /**************************************************************************************/#include "FN_knot_surface.h"    //contains some functions and all global variables#include <omp.h>#define RK4 1         //1 to use Runge Kutta 4th order method, 0 for euler forward method/*Available options: FROM_PHI_FILE: Skip initialisation, input from previous run. FROM_SURFACE_FILE: Initialise from input file(s) generated in surface evolver.  FROM_UV_FILE: Skip initialisation, run FN dynamics from uv file */unsigned int option = FROM_SURFACE_FILE;         //unknot default optionconst bool periodic = false;                     //enable periodic boundaries in z//const bool addtwist = false;/**If FROM_SURFACE_FILE chosen**/string knot_filename = "Unlink";      //assumed input filename format of "XXXXX.stl"/**IF FROM_PHI_FILE or FROM_UV_FILE chosen**/string B_filename = "uvplot_in.vtk";    //filename for phi field or uv field//Grid pointsconst unsigned int Nx = 240;   //No. points in x,y and zconst unsigned int Ny = 240;const unsigned int Nz = 180;const double TTime = 3000;         //total time of simulation (simulation units)const double skiptime = 500;       //print out every # unit of time (simulation units)const double starttime = 0;        //Time at start of simulation (non-zero if continuing from UV file)const double dtime = 0.05;         //size of each time step//System size parametersconst double lambda = 21.3;                //approx wavelengthdouble size = 10*lambda;           //box sizedouble h = size/(Nx-1);            //grid spacingdouble coresize = lambda/(2*M_PI);              //approx core diameter (for skipping points in initialisation)const double epsilon = 0.3;                //parameters for F-N eqnsconst double beta = 0.7;const double gam = 0.5;//Size boundaries of knot (now autoscaled)double xmax = Nx*h/2.0;double ymax = Ny*h/2.0;double zmax = 2*Nz*h/3.0;unsigned int NK;   //number of surface points//Unallocated matricesvector<triangle> knotsurface;    //structure for storing knot surface coordinatesdouble area;   //initial knot areainline unsigned int pt(unsigned int i, unsigned int j, unsigned int k)       //convert i,j,k to single index{    return (i*Ny*Nz+j*Nz+k);}int main (void){    double *x, *y, *z, *phi, *u, *v, *ucv, *d2u, *d2v;    unsigned int *missed;    unsigned int i,j,k,n,l;        x = new double [Nx];    y = new double [Ny];    z = new double [Nz];    u = new double [Nx*Ny*Nz];    v = new double [Nx*Ny*Nz];    ucv = new double [Nx*Ny*Nz];    d2u = new double [Nx*Ny*Nz];    d2v = new double [Nx*Ny*Nz];    phi = new double [Nx*Ny*Nz];  //scalar potential    missed = new unsigned int [Nx*Ny*Nz];    	// output an info file on the run	print_info(Nx, Ny, Nz, dtime, h, periodic, option, knot_filename, B_filename);	        # pragma omp parallel shared ( x, y, z ) private ( i, j, k )    {#pragma omp for        for(i=0;i<Nx;i++)           //initialise grid        {            x[i] = (i+0.5-Nx/2.0)*h;        }#pragma omp for        for(j=0;j<Ny;j++)        {            y[j] = (j+0.5-Ny/2.0)*h;        }#pragma omp for        for(k=0;k<Nz;k++)        {            z[k] = (k+0.5-Nz/2.0)*h;        }    }        if (option == FROM_PHI_FILE)    {        cout << "Reading input file...\n";        phi_file_read(phi,missed);    }    else    {        if(option == FROM_UV_FILE)        {            cout << "Reading input file...\n";            if(uvfile_read(u,v)) return 1;        }        else        {            //Initialise knot            area = initialise_knot();            if(area==0)            {                cout << "Error reading input option. Aborting...\n";                return 1;            }                        cout << "Total no. of surface points: " << NK << endl;                        //Calculate phi for initial conditions            initial_cond(x,y,z,phi,missed);        }    }        if(option!=FROM_UV_FILE)    {        cout << "Calculating u and v...\n";        uv_initialise(phi,u,v,ucv,missed);    }        delete [] missed;    delete [] phi;    delete [] x;    delete [] y;    delete [] z;        #if RK4    double **ku, **kv, *uold, *vold;    ku = new double* [4];    kv = new double* [4];    uold = new double [Nx*Ny*Nz];    vold = new double [Nx*Ny*Nz];        for(l=0;l<4;l++)    {        ku[l] = new double [Nx*Ny*Nz];        kv[l] = new double [Nx*Ny*Nz];    }#else    double *D2u;        D2u = new double [Nx*Ny*Nz];#endif        cout << "Updating u and v...\n";        // initilialising counters     unsigned int p=0;    n=0;        // initialising timers    time_t then = time(NULL); 	time_t rawtime;	time (&rawtime);	struct tm * timeinfo;    #if RK4    #pragma omp parallel default(none) shared ( u, v, uold, vold, n,ku,kv,p,ucv, cout, rawtime, timeinfo )#else    #pragma omp parallel default(none) shared ( u, v, n, D2u, p, ucv, cout, rawtime, timeinfo )#endif    {		while(n*dtime<=TTime)		{			#pragma omp single			{				if(n*dtime >= p*skiptime)				{					crossgrad_calc(u,v,ucv);					print_uv(u,v,ucv,n*dtime+starttime);					cout << "T = " << n*dtime + starttime << endl;					time (&rawtime);					timeinfo = localtime (&rawtime);					cout << "current time \t" << asctime(timeinfo) << "\n";					p++;				}				n++;			}#if RK4			uv_update(u,v,ku,kv,uold,vold);#else            uv_update_euler(u,v,D2u);#endif		}	}    time_t now = time(NULL);    cout << "Time taken to complete uv part: " << now - then << " seconds.\n";    #if RK4    for(l=0;l<4;l++)    {        delete [] ku[l];        delete [] kv[l];    }        delete [] uold;    delete [] vold;    delete [] ku;    delete [] kv;#else    delete [] D2u;#endif    delete [] u;    delete [] v;    delete [] ucv;    delete [] d2u;    delete [] d2v;        return 0;}/*************************Functions for knot initialisation*****************************/double initialise_knot(){    double L;    switch (option)    {        case FROM_SURFACE_FILE: L = init_from_surface_file();            break;                default: L=0;            break;    }        return L;}double init_from_surface_file(void){    string filename, buff;    stringstream ss;    double A = 0;   //total area    unsigned int i=0;    unsigned int j;    double r10,r20,r21,s,xcoord,ycoord,zcoord;    string temp;    ifstream knotin;    /*  For recording max and min input values*/    double maxxin = 0;    double maxyin = 0;    double maxzin = 0;    double minxin = 0;    double minyin = 0;    double minzin = 0;        ss.clear();    ss.str("");    ss << knot_filename << ".stl";        filename = ss.str();    knotin.open(filename.c_str());    if(knotin.good())    {        if(getline(knotin,buff)) temp = buff;    }    else cout << "Error reading file\n";    while(knotin.good())   //read in points for knot    {        if(getline(knotin,buff))  //read in surface normal        {            ss.clear();            ss.str("");            ss << buff;            ss >> temp;            if(temp.compare("endsolid") == 0) break;            knotsurface.push_back(triangle());            ss >> temp >> knotsurface[i].normal[0] >> knotsurface[i].normal[1] >> knotsurface[i].normal[2];        }                if(getline(knotin,buff)) temp = buff;   //read in "outer loop"        knotsurface[i].centre[0] = 0;        knotsurface[i].centre[1] = 0;        knotsurface[i].centre[2] = 0;        for(j=0;j<3;j++)        {            if(getline(knotin,buff))  //read in vertices            {                ss.clear();                ss.str("");                ss << buff;                ss >> temp >> xcoord >> ycoord >> zcoord;                                if(xcoord>maxxin) maxxin = xcoord;                if(ycoord>maxyin) maxyin = ycoord;                if(zcoord>maxzin) maxzin = zcoord;                if(xcoord<minxin) minxin = xcoord;                if(ycoord<minyin) minyin = ycoord;                if(zcoord<minzin) minzin = zcoord;                                knotsurface[i].xvertex[j] = xcoord;                knotsurface[i].yvertex[j] = ycoord;                knotsurface[i].zvertex[j] = zcoord;                knotsurface[i].centre[0] += knotsurface[i].xvertex[j]/3;                knotsurface[i].centre[1] += knotsurface[i].yvertex[j]/3;                knotsurface[i].centre[2] += knotsurface[i].zvertex[j]/3;            }        }        //cout << i << " (" << knotsurface[i].centre[0] << ',' << knotsurface[i].centre[1] << ',' << knotsurface[i].centre[2] << ") , (" << knotsurface[i].normal[0] << ',' << knotsurface[i].normal[1] << ',' << knotsurface[i].normal[2] << ") \n";                if(getline(knotin,buff)) temp = buff;   //read in "outer loop"        if(getline(knotin,buff)) temp = buff;   //read in "outer loop"        i++;    }        NK = i;    /* Work out space scaling for knot surface */    double scale[3];    if(maxxin-minxin>0) scale[0] = xmax/(maxxin-minxin);    else scale[0] = 1;    if(maxyin-minyin>0) scale[1] = ymax/(maxyin-minyin);    else scale[1] = 1;    if(maxzin-minzin>0) scale[2] = zmax/(maxzin-minzin);    else scale[2] = 1;    double midpoint[3];    double norm;    //double p1x,p1y,p1z,p2x,p2y,p2z,nx,ny,nz;    midpoint[0] = 0.5*(maxxin+minxin);    midpoint[1] = 0.5*(maxyin+minyin);    midpoint[2] = 0.5*(maxzin+minzin);        /*Rescale points and normals to fit grid properly*/    for(i=0;i<NK;i++)    {        for(j=0;j<3;j++)        {            knotsurface[i].xvertex[j] = scale[0]*(knotsurface[i].xvertex[j] - midpoint[0]);            knotsurface[i].yvertex[j] = scale[1]*(knotsurface[i].yvertex[j] - midpoint[1]);            knotsurface[i].zvertex[j] = scale[2]*(knotsurface[i].zvertex[j] - midpoint[2]);            knotsurface[i].centre[j] = scale[j]*(knotsurface[i].centre[j] - midpoint[j]);        }                norm = sqrt(scale[1]*scale[1]*scale[2]*scale[2]*knotsurface[i].normal[0]*knotsurface[i].normal[0] +                    scale[0]*scale[0]*scale[2]*scale[2]*knotsurface[i].normal[1]*knotsurface[i].normal[1] +                    scale[0]*scale[0]*scale[1]*scale[1]*knotsurface[i].normal[2]*knotsurface[i].normal[2]);                knotsurface[i].normal[0] *= scale[1]*scale[2]/norm;        knotsurface[i].normal[1] *= scale[0]*scale[2]/norm;        knotsurface[i].normal[2] *= scale[0]*scale[1]/norm;                /*Check surface normal is correct        p1x = knotsurface[i].xvertex[1] - knotsurface[i].xvertex[0];        p1y = knotsurface[i].yvertex[1] - knotsurface[i].yvertex[0];        p1z = knotsurface[i].zvertex[1] - knotsurface[i].zvertex[0];        p2x = knotsurface[i].xvertex[2] - knotsurface[i].xvertex[0];        p2y = knotsurface[i].yvertex[2] - knotsurface[i].yvertex[0];        p2z = knotsurface[i].zvertex[2] - knotsurface[i].zvertex[0];        nx = p1y*p2z - p2y*p1z;        ny = p1z*p2x - p2z*p1x;        nz = p1x*p2y - p2x*p1y;        norm = sqrt(nx*nx+ny*ny+nz*nz);        nx = nx/norm;        ny = ny/norm;        nz = nz/norm;        cout << nx*knotsurface[i].normal[0] + ny*knotsurface[i].normal[1] + nz*knotsurface[i].normal[2] << '\n';        */                r10 = sqrt((knotsurface[i].xvertex[1]-knotsurface[i].xvertex[0])*(knotsurface[i].xvertex[1]-knotsurface[i].xvertex[0]) + (knotsurface[i].yvertex[1]-knotsurface[i].yvertex[0])*(knotsurface[i].yvertex[1]-knotsurface[i].yvertex[0]) + (knotsurface[i].zvertex[1]-knotsurface[i].zvertex[0])*(knotsurface[i].zvertex[1]-knotsurface[i].zvertex[0]));        r20 = sqrt((knotsurface[i].xvertex[2]-knotsurface[i].xvertex[0])*(knotsurface[i].xvertex[2]-knotsurface[i].xvertex[0]) + (knotsurface[i].yvertex[2]-knotsurface[i].yvertex[0])*(knotsurface[i].yvertex[2]-knotsurface[i].yvertex[0]) + (knotsurface[i].zvertex[2]-knotsurface[i].zvertex[0])*(knotsurface[i].zvertex[2]-knotsurface[i].zvertex[0]));        r21 = sqrt((knotsurface[i].xvertex[2]-knotsurface[i].xvertex[1])*(knotsurface[i].xvertex[2]-knotsurface[i].xvertex[1]) + (knotsurface[i].yvertex[2]-knotsurface[i].yvertex[1])*(knotsurface[i].yvertex[2]-knotsurface[i].yvertex[1]) + (knotsurface[i].zvertex[2]-knotsurface[i].zvertex[1])*(knotsurface[i].zvertex[2]-knotsurface[i].zvertex[1]));        s = (r10+r20+r21)/2;        knotsurface[i].area = sqrt(s*(s-r10)*(s-r20)*(s-r21));        A += knotsurface[i].area;    }        cout << "Input scaled by: " << scale[0] << ' ' << scale[1] << ' ' << scale[2] << "in x,y and z\n";        return A;    }/*************************Functions for B and Phi calcs*****************************/void initial_cond(double *x, double *y, double *z, double *phi, unsigned int *missed){    unsigned int *ignore;  //Points to ignore    unsigned int *ignore1;    double *Bx;  //Mag field    double *By;    double *Bz;    double *Bmag;        ignore = new unsigned int [Nx*Ny*Nz];    ignore1 = new unsigned int [Nx*Ny*Nz];    Bx = new double [Nx*Ny*Nz];    By = new double [Nx*Ny*Nz];    Bz = new double [Nx*Ny*Nz];    Bmag = new double [Nx*Ny*Nz];        cout << "Calculating scalar potential...\n";    time_t then = time(NULL);    phi_calc(x,y,z,missed, phi);    time_t now = time(NULL);    cout << "Initialisation took " << now - then << " seconds.\n";    cout << "Printing B and phi...\n";    print_B_phi(x, y, z, missed, phi);        delete [] ignore;    delete [] ignore1;    delete [] Bx;    delete [] By;    delete [] Bz;    delete [] Bmag;}void phi_calc(double *x, double *y, double *z, unsigned int *missed, double *phi){    unsigned int i,j,k,n,s;    double rx,ry,rz,r;        #pragma omp parallel shared ( x, y, z, knotsurface, missed, phi, coresize ) private ( i, j, k, n, s, rx, ry, rz , r)    {#pragma omp for        for(i=0;i<Nx;i++)        {            for(j=0; j<Ny; j++)            {                for(k=0; k<Nz; k++)                {                    n = pt(i,j,k);                    missed[n] = 0;                    phi[n] = 0;                    for(s=0;s<NK;s++)                    {                        rx = knotsurface[s].centre[0]-x[i];                        ry = knotsurface[s].centre[1]-y[j];                        rz = knotsurface[s].centre[2]-z[k];                        r = sqrt(rx*rx+ry*ry+rz*rz);                        if(r<coresize) missed[n] = 1;                        if(r>0) phi[n] += (rx*knotsurface[s].normal[0] + ry*knotsurface[s].normal[1] + rz*knotsurface[s].normal[2])*knotsurface[s].area/(2*r*r*r);                    }                    if(missed[n]) phi[n] = M_PI;                    if(phi[n]>M_PI) phi[n] = M_PI;                    if(phi[n]<-M_PI) phi[n] = -M_PI;                }            }        }    }    }/*************************Functions for FN dynamics*****************************/void uv_initialise(double *phi, double *u, double *v, double *ucv, unsigned int *missed){    unsigned int n;        for(n=0; n<Nx*Ny*Nz; n++)    {        u[n] = (2*cos(phi[n]) - 0.4);        v[n] = (sin(phi[n]) - 0.4);        //if missed set value to -0.4 (vortex centre value)    }}void crossgrad_calc(double *u, double *v, double *ucv){    unsigned int i,j,k,kup,kdwn;    double dxu,dyu,dzu,dxv,dyv,dzv,ucvx,ucvy,ucvz;	for(i=0;i<Nx;i++)	{		for(j=0; j<Ny; j++)		{			for(k=0; k<Nz; k++)   //Central difference			{				if(periodic)   //check for periodic boundaries				{					kup = incp(k,1,Nz);					kdwn = incp(k,-1,Nz);				}				else				{					kup = incw(k,1,Nz);					kdwn = incw(k,-1,Nz);				}				dxu = 0.5*(u[pt(incw(i,1,Nx),j,k)]-u[pt(incw(i,-1,Nx),j,k)])/h;				dxv = 0.5*(v[pt(incw(i,1,Nx),j,k)]-v[pt(incw(i,-1,Nx),j,k)])/h;				dyu = 0.5*(u[pt(i,incw(j,1,Ny),k)]-u[pt(i,incw(j,-1,Ny),k)])/h;				dyv = 0.5*(v[pt(i,incw(j,1,Ny),k)]-v[pt(i,incw(j,-1,Ny),k)])/h;				dzu = 0.5*(u[pt(i,j,kup)]-u[pt(i,j,kdwn)])/h;				dzv = 0.5*(v[pt(i,j,kup)]-v[pt(i,j,kdwn)])/h;				ucvx = dyu*dzv - dzu*dyv;				ucvy = dzu*dxv - dxu*dzv;				ucvz = dxu*dyv - dyu*dxv;				ucv[pt(i,j,k)] = sqrt(ucvx*ucvx + ucvy*ucvy + ucvz*ucvz);			}		}	}}void uv_update(double *u, double *v, double **ku, double **kv, double *uold, double *vold){    unsigned int i,j,k,l,n,kup,kdwn;    double D2u;    #pragma omp for    for(i=0;i<Nx;i++)    {        for(j=0; j<Ny; j++)        {            for(k=0; k<Nz; k++)            {                n = pt(i,j,k);                uold[n] = u[n];  //old value of u                vold[n] = v[n];  //old value of v            }        }    }            for(l=0;l<4;l++)  //u and v update for each fractional time step    {        #pragma omp for        for(i=0;i<Nx;i++)        {            for(j=0; j<Ny; j++)            {                for(k=0; k<Nz; k++)   //Central difference                {                    if(periodic)   //check for periodic boundaries                    {                        kup = incp(k,1,Nz);                        kdwn = incp(k,-1,Nz);                    }                    else                    {                        kup = incw(k,1,Nz);                        kdwn = incw(k,-1,Nz);                    }                    n = pt(i,j,k);                    D2u = (u[pt(incw(i,1,Nx),j,k)] + u[pt(incw(i,-1,Nx),j,k)] + u[pt(i,incw(j,1,Ny),k)] + u[pt(i,incw(j,-1,Ny),k)] + u[pt(i,j,kup)] + u[pt(i,j,kdwn)] - 6*u[n])/(h*h);                    ku[l][n] = (u[n] - u[n]*u[n]*u[n]/3 - v[n])/epsilon + D2u;                    kv[l][n] = epsilon*(u[n] + beta - gam*v[n]);                }            }        }        #pragma omp for        for(i=0;i<Nx;i++)        {            for(j=0; j<Ny; j++)            {                for(k=0; k<Nz; k++)  //update                {                    n = pt(i,j,k);                    if(l==0 || l==1)                    {                        u[n] = uold[n] + 0.5*dtime*ku[l][n];                        v[n] = vold[n] + 0.5*dtime*kv[l][n];                    }                    else                    {                        if(l==2)                        {                            u[n] = uold[n] + dtime*ku[l][n];                            v[n] = vold[n] + dtime*kv[l][n];                        }                        else                        {                            u[n] = uold[n] + dtime*(ku[0][n] + 2*ku[1][n] + 2*ku[2][n] + ku[3][n])/6;                            v[n] = vold[n] + dtime*(kv[0][n] + 2*kv[1][n] + 2*kv[2][n] + kv[3][n])/6;                        }                    }                }            }        }            }}void uv_update_euler(double *u, double *v, double *D2u){    unsigned int i,j,k,l,n,kup,kdwn;    #pragma omp for    for(i=0;i<Nx;i++)    {        for(j=0; j<Ny; j++)        {            for(k=0; k<Nz; k++)            {                n = pt(i,j,k);                if(periodic)   //check for periodic boundaries                {                    kup = incp(k,1,Nz);                    kdwn = incp(k,-1,Nz);                }                else                {                    kup = incw(k,1,Nz);                    kdwn = incw(k,-1,Nz);                }                D2u[n] = (u[pt(incw(i,1,Nx),j,k)] + u[pt(incw(i,-1,Nx),j,k)] + u[pt(i,incw(j,1,Ny),k)] + u[pt(i,incw(j,-1,Ny),k)] + u[pt(i,j,kup)] + u[pt(i,j,kdwn)] - 6*u[n])/(h*h);            }        }    }    #pragma omp for    for(i=0;i<Nx;i++)    {        for(j=0; j<Ny; j++)        {            for(k=0; k<Nz; k++)            {                n = pt(i,j,k);                u[n] = u[n] + dtime*((u[n] - u[n]*u[n]*u[n]/3 - v[n])/epsilon + D2u[n]);                v[n] = v[n] + dtime*(epsilon*(u[n] + beta - gam*v[n]));            }        }    }}/*************************File reading and writing*****************************/void print_uv(double *u, double *v, double *ucv, double t){    unsigned int i,j,k,n;    stringstream ss;    ss << "uv_plot" << t << ".vtk";    ofstream uvout (ss.str().c_str());        uvout << "# vtk DataFile Version 3.0\nUV fields\nASCII\nDATASET STRUCTURED_POINTS\n";    uvout << "DIMENSIONS " << Nx << ' ' << Ny << ' ' << Nz << '\n';    uvout << "ORIGIN " << -h*(Nx/2.0 + 0.5) << ' ' << -h*(Ny/2.0 + 0.5) << ' ' << -h*(Nz/2.0 + 0.5) << '\n';    uvout << "SPACING " << h << ' ' << h << ' ' << h << '\n';    uvout << "POINT_DATA " << Nx*Ny*Nz << '\n';    uvout << "SCALARS u float\nLOOKUP_TABLE default\n";            for(k=0; k<Nz; k++)    {        for(j=0; j<Ny; j++)        {            for(i=0; i<Nx; i++)            {                n = pt(i,j,k);                uvout << u[n] << '\n';            }        }    }        uvout << "SCALARS v float\nLOOKUP_TABLE default\n";            for(k=0; k<Nz; k++)    {        for(j=0; j<Ny; j++)        {            for(i=0; i<Nx; i++)            {                n = pt(i,j,k);                uvout << v[n] << '\n';            }        }    }        uvout << "SCALARS ucrossv float\nLOOKUP_TABLE default\n";        for(k=0; k<Nz; k++)    {        for(j=0; j<Ny; j++)        {            for(i=0; i<Nx; i++)            {                n = pt(i,j,k);                uvout << ucv[n] << '\n';            }        }    }        uvout.close();}void print_B_phi(double *x, double *y, double*z, unsigned int *missed, double *phi){    unsigned int i,j,k,n;    string fn = "phi.vtk";        ofstream Bout (fn.c_str());        Bout << "# vtk DataFile Version 3.0\nKnot\nASCII\nDATASET STRUCTURED_POINTS\n";    Bout << "DIMENSIONS " << Nx << ' ' << Ny << ' ' << Nz << '\n';    Bout << "ORIGIN " << -h*(Nx/2.0 + 0.5) << ' ' << -h*(Ny/2.0 + 0.5) << ' ' << -h*(Nz/2.0 + 0.5) << '\n';    Bout << "SPACING " << h << ' ' << h << ' ' << h << '\n';    Bout << "POINT_DATA " << Nx*Ny*Nz << '\n';    Bout << "SCALARS Phi float\nLOOKUP_TABLE default\n";    for(k=0; k<Nz; k++)    {        for(j=0; j<Ny; j++)        {            for(i=0; i<Nx; i++)            {                n = pt(i,j,k);                Bout << phi[n] << '\n';            }        }    }        Bout << "SCALARS Missed float\nLOOKUP_TABLE default\n";    for(k=0; k<Nz; k++)    {        for(j=0; j<Ny; j++)        {            for(i=0; i<Nx; i++)            {                n = pt(i,j,k);                Bout << missed[n] << '\n';            }        }    }        Bout.close();}void print_info(int Nx, int Ny, int Nz, double dtime, double h, const bool periodic, unsigned int option, string knot_filename, string B_filename){	string fn = "info.txt";	time_t rawtime;	struct tm * timeinfo;	time (&rawtime);	timeinfo = localtime (&rawtime);	ofstream infoout (fn.c_str());	infoout << "run started at\t" << asctime(timeinfo) << "\n";	infoout << "Number of grid points\t" << Nx << '\t' << Ny << '\t' << Nz << '\n';	infoout << "timestep\t" << dtime << '\n';	infoout << "Spacing\t" << h << '\n';	infoout << "Periodic\t" << periodic << '\n';	infoout << "initoptions\t" << option << '\n';  	infoout << "knot filename\t" << knot_filename << '\n';	infoout << "B or uv filename\t" << B_filename << '\n';   	infoout.close();}int phi_file_read(double *phi, unsigned int *missed){    string temp,buff;    stringstream ss;    ifstream fin (B_filename.c_str());    unsigned int i,j,k,n;        for(i=0;i<10;i++)    {        if(fin.good())        {            if(getline(fin,buff)) temp = buff;        }        else        {            cout << "Something went wrong!\n";            return 1;        }    }        for(k=0; k<Nz; k++)    {        for(j=0; j<Ny; j++)        {            for(i=0; i<Nx; i++)            {                n=pt(i,j,k);                ss.clear();                ss.str("");                if(fin.good())                {                    if(getline(fin,buff))                    {                        ss << buff;                        ss >> phi[n];                    }                }                else                {                    cout << "Something went wrong!\n";                    return 1;                }            }        }    }        for(i=0;i<2;i++)    {        if(fin.good())        {            if(getline(fin,buff)) temp = buff;        }        else        {            cout << "Something went wrong!\n";            return 1;        }    }        for(k=0; k<Nz; k++)    {        for(j=0; j<Ny; j++)        {            for(i=0; i<Nx; i++)            {                n=pt(i,j,k);                ss.clear();                ss.str("");                if(fin.good())                {                    if(getline(fin,buff)) ss << buff;                    ss >> missed[n];                }                else                {                    cout << "Something went wrong!\n";                    return 1;                }            }        }    }        fin.close();        return 0;}int uvfile_read(double *u, double *v){    string temp,buff;    stringstream ss;    ifstream fin (B_filename.c_str());    unsigned int i,j,k,n;        for(i=0;i<10;i++)    {        if(fin.good())        {            if(getline(fin,buff)) temp = buff;        }        else        {            cout << "Something went wrong!\n";            return 1;        }    }        for(k=0; k<Nz; k++)    {        for(j=0; j<Ny; j++)        {            for(i=0; i<Nx; i++)            {                n=pt(i,j,k);                ss.clear();                ss.str("");                if(fin.good())                {                    if(getline(fin,buff))                    {                        ss << buff;                        ss >> u[n];                    }                }                else                {                    cout << "Something went wrong!\n";                    return 1;                }            }        }    }        for(i=0;i<2;i++)    {        if(fin.good())        {            if(getline(fin,buff)) temp = buff;        }        else        {            cout << "Something went wrong!\n";            return 1;        }    }        for(k=0; k<Nz; k++)    {        for(j=0; j<Ny; j++)        {            for(i=0; i<Nx; i++)            {                n=pt(i,j,k);                ss.clear();                ss.str("");                if(fin.good())                {                    if(getline(fin,buff)) ss << buff;                    ss >> v[n];                }                else                {                    cout << "Something went wrong!\n";                    return 1;                }            }        }    }        fin.close();        return 0;}>>>>>>> origin/master