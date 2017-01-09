/**************************************************************************************/
/* Fitzhugh-Nagumo reaction diffusion simulation with arbitrary vortex lines
   OPENMP VERSION
   Created by Carl Whitfield
   Last modified 30/11/16

   Operational order of the code:
   1) The code takes as input an stl file (defined in knot_filename) which defines an orientable surface with a boundary.
   2) This surface is scaled to fill a box of size xmax x ymax x zmax.
   3) A nunmerical integral is performed to calculate a phase field (phi_calc) on the 3D grid which winds around the boundary of the surface.
   4) This is then used to initialise the Fitzhugh-Nagumo set of partial differential equations such that on initialisation (uv_initialise):
   u = 2cos(phi) - 0.4        v = sin(phi) - 0.4
   The pde's used are
   dudt = (u - u^3/3 - v)/epsilon + Del^2 u
   dvdt = epsilon*(u + beta - gam v)
   5) The update method is Runge-Kutta fourth order (update_uv) unless RK4 is set to 0, otherwise Euler forward method is used.
   6) A parametric curve for the knot is found at each unit T


   The parameters epsilon, beta and gam are set to give rise to scroll waves (see Sutcliffe, Winfree, PRE 2003 and Maucher Sutcliffe PRL 2016) which eminate from a closed curve, on initialisation this curve corresponds to the boundary of the surface in the stl file.

   See below for various options to start the code from previous outputted data.*/
/**************************************************************************************/
#include "FN_knot_surface.h"    //contains some functions and all global variables
#include <omp.h>
#define RK4 1         //1 to use Runge Kutta 4th order method, 0 for euler forward method
//includes for the signal processing
#include <gsl/gsl_multimin.h>
#include <gsl/gsl_vector.h>

/*Available options:
FROM_PHI_FILE: Skip initialisation, input from previous run.
FROM_SURFACE_FILE: Initialise from input file(s) generated in surface evolver.
FROM_UV_FILE: Skip initialisation, run FN dynamics from uv file
 */
int option = FROM_UV_FILE;         //unknot default option
const bool periodic = false;                     //enable periodic boundaries in z
//const bool addtwist = false;

/**If FROM_SURFACE_FILE chosen**/
string knot_filename = "five2";      //assumed input filename format of "XXXXX.stl"


/**IF FROM_PHI_FILE or FROM_UV_FILE chosen**/
string B_filename = "uv_plot60.vtk";    //filename for phi field or uv field

//Grid points
const int Nx = 200;   //No. points in x,y and z
const int Ny = 200;
const int Nz = 200;
const double TTime = 400;         //total time of simulation (simulation units)
const double skiptime = 50;       //print out every # unit of time (simulation units)
const double starttime = 60;        //Time at start of simulation (non-zero if continuing from UV file)
const double dtime = 0.02;         //size of each time step

//System size parameters
const double lambda = 21.3;                //approx wavelength
const double size = 6*lambda;           //box size
const double h = size/(Nx-1);            //grid spacing
const double oneoverhsq = 1.0/(h*h);
const double epsilon = 0.3;                //parameters for F-N eqns
const double oneoverepsilon = 1.0/epsilon;
const double beta = 0.7;
const double gam = 0.5;

//Size boundaries of knot (now autoscaled)
double xmax = 3*Nx*h/4.0;
double ymax = 3*Ny*h/4.0;
double zmax = 3*Nz*h/4.0;
int NK;   //number of surface points

//Unallocated matrices
vector<triangle> knotsurface;    //structure for storing knot surface coordinates
vector<knotpoint> knotcurve;
bool knotexists = true; //Always true at start


double area;   //initial knot area
inline  int pt( int i,  int j,  int k)       //convert i,j,k to single index
{
	return (i*Ny*Nz+j*Nz+k);
}

int main (void)
{
	double *x, *y, *z, *phi, *u, *v, *ucvx, *ucvy, *ucvz;
	int i,j,k,n,l;

	x = new double [Nx];
	y = new double [Ny];
	z = new double [Nz];
	phi = new double [Nx*Ny*Nz];  //scalar potential
	u = new double [Nx*Ny*Nz];
	v = new double [Nx*Ny*Nz];

	// output an info file on the run
	print_info(Nx, Ny, Nz, dtime, h, periodic, option, knot_filename, B_filename);

	// GSL initialization
	const gsl_multimin_fdfminimizer_type *Type;
	gsl_multimin_fdfminimizer *minimizerstate;

	Type = gsl_multimin_fdfminimizer_conjugate_fr;
	minimizerstate = gsl_multimin_fdfminimizer_alloc (Type, 3);


# pragma omp parallel shared ( x, y, z ) private ( i, j, k )
	{
#pragma omp for
		for(i=0;i<Nx;i++)           //initialise grid
		{
			x[i] = (i+0.5-Nx/2.0)*h;
		}
#pragma omp for
		for(j=0;j<Ny;j++)
		{
			y[j] = (j+0.5-Ny/2.0)*h;
		}
#pragma omp for
		for(k=0;k<Nz;k++)
		{
			z[k] = (k+0.5-Nz/2.0)*h;
		}
	}

	if (option == FROM_PHI_FILE)
	{
		cout << "Reading input file...\n";
		phi_file_read(phi);
	}
	else
	{
		if(option == FROM_UV_FILE)
		{
			cout << "Reading input file...\n";
			if(uvfile_read(u,v)) return 1;
		}
		else
		{
			//Initialise knot
			area = initialise_knot();
			if(area==0)
			{
				cout << "Error reading input option. Aborting...\n";
				return 1;
			}

			cout << "Total no. of surface points: " << NK << endl;

			//Calculate phi for initial conditions
			initial_cond(x,y,z,phi);
		}

	}

	vector<triangle> ().swap(knotsurface);   //empty knotsurface memory

	if(option!=FROM_UV_FILE)
	{
		cout << "Calculating u and v...\n";
		uv_initialise(phi,u,v);
	}

	delete [] phi;

	ucvx = new double [Nx*Ny*Nz];
	ucvy = new double [Nx*Ny*Nz];
	ucvz = new double [Nx*Ny*Nz];
#if RK4
	double *ku, *kv, *kut, *kvt, *uold, *vold;
	ku = new double [Nx*Ny*Nz];
	kv = new double [Nx*Ny*Nz];
	kut = new double [Nx*Ny*Nz];
	kvt = new double [Nx*Ny*Nz];
	uold = new double [Nx*Ny*Nz];
	vold = new double [Nx*Ny*Nz];

#else
	double *D2u;

	D2u = new double [Nx*Ny*Nz];
#endif

	cout << "Updating u and v...\n";

	// initilialising counters
	int p=0;
	int q=0;
	n=0;

	// initialising timers
	time_t then = time(NULL);
	time_t rawtime;
	time (&rawtime);
	struct tm * timeinfo;
	ofstream wrout;
	wrout.open("writhe.txt");
	wrout << "Time\tWrithe\tTwist\tLength\n";
	wrout.close();

#if RK4
#pragma omp parallel default(none) shared ( x, y, z, u, v, uold, vold, n,ku,kv,kut,kvt,p,q,ucvx, ucvy, ucvz,cout, rawtime, timeinfo, knotcurve, knotexists,minimizerstate )
#else
#pragma omp parallel default(none) shared ( x, y, z, u, v, n, D2u, p, q,ucvx, ucvy, ucvz, cout, rawtime, timeinfo, knotcurve, knotexists , minimizerstate)
#endif
	{
		while(n*dtime <= TTime)
		{
#pragma omp single
			{
				if(n*dtime >= q)  //Do this every unit T
				{
					if(knotexists) knotcurve.clear(); //empty vector with knot curve points
					crossgrad_calc(x,y,z,u,v,ucvx,ucvy,ucvz); //find Grad u cross Grad v
					cout << "T = " << n*dtime + starttime << endl;
					time (&rawtime);
					timeinfo = localtime (&rawtime);
					cout << "current time \t" << asctime(timeinfo) << "\n";
					if(n*dtime+starttime>=10 && knotexists)
					{
					//	find_knot_properties(x,y,z,ucvx,ucvy,ucvz,u,n*dtime+starttime,minimizerstate);      //find knot curve and twist and writhe
					//	print_knot(x,y,z,n*dtime+starttime);
					}
					q++;
				}

				if(n*dtime >= p*skiptime)
				{

					print_uv(x,y,z,u,v,ucvx,ucvy,ucvz,n*dtime+starttime);
					p++;
				}

				n++;
			}
#if RK4
			uv_update(u,v,ku,kv,kut,kvt,uold,vold);
#else
			uv_update_euler(u,v,D2u);
#endif
		}
	}
	time_t now = time(NULL);
	cout << "Time taken to complete uv part: " << now - then << " seconds.\n";

#if RK4
	delete [] uold;
	delete [] vold;
	delete [] ku;
	delete [] kv;
	delete [] kut;
	delete [] kvt;
#else
	delete [] D2u;
#endif
	delete [] x;
	delete [] y;
	delete [] z;
	delete [] u;
	delete [] v;
	delete [] ucvx;
	delete [] ucvy;
	delete [] ucvz;

	return 0;
}

/*************************Functions for knot initialisation*****************************/
double initialise_knot()
{
	double L;
	switch (option)
	{
		case FROM_SURFACE_FILE: L = init_from_surface_file();
					break;

		default: L=0;
			 break;
	}

	return L;
}

double init_from_surface_file(void)
{
	string filename, buff;
	stringstream ss;
	double A = 0;   //total area
	int i=0;
	int j;
	double r10,r20,r21,s,xcoord,ycoord,zcoord;
	string temp;
	ifstream knotin;
	/*  For recording max and min input values*/
	double maxxin = 0;
	double maxyin = 0;
	double maxzin = 0;
	double minxin = 0;
	double minyin = 0;
	double minzin = 0;

	ss.clear();
	ss.str("");
	ss << knot_filename << ".stl";

	filename = ss.str();
	knotin.open(filename.c_str());
	if(knotin.good())
	{
		if(getline(knotin,buff)) temp = buff;
	}
	else cout << "Error reading file\n";
	while(knotin.good())   //read in points for knot
	{
		if(getline(knotin,buff))  //read in surface normal
		{
			ss.clear();
			ss.str("");
			ss << buff;
			ss >> temp;
			if(temp.compare("endsolid") == 0) break;
			knotsurface.push_back(triangle());
			ss >> temp >> knotsurface[i].normal[0] >> knotsurface[i].normal[1] >> knotsurface[i].normal[2];
		}

		if(getline(knotin,buff)) temp = buff;   //read in "outer loop"
		knotsurface[i].centre[0] = 0;
		knotsurface[i].centre[1] = 0;
		knotsurface[i].centre[2] = 0;
		for(j=0;j<3;j++)
		{
			if(getline(knotin,buff))  //read in vertices
			{
				ss.clear();
				ss.str("");
				ss << buff;
				ss >> temp >> xcoord >> ycoord >> zcoord;

				if(xcoord>maxxin) maxxin = xcoord;
				if(ycoord>maxyin) maxyin = ycoord;
				if(zcoord>maxzin) maxzin = zcoord;
				if(xcoord<minxin) minxin = xcoord;
				if(ycoord<minyin) minyin = ycoord;
				if(zcoord<minzin) minzin = zcoord;

				knotsurface[i].xvertex[j] = xcoord;
				knotsurface[i].yvertex[j] = ycoord;
				knotsurface[i].zvertex[j] = zcoord;
				knotsurface[i].centre[0] += knotsurface[i].xvertex[j]/3;
				knotsurface[i].centre[1] += knotsurface[i].yvertex[j]/3;
				knotsurface[i].centre[2] += knotsurface[i].zvertex[j]/3;
			}
		}
		//cout << i << " (" << knotsurface[i].centre[0] << ',' << knotsurface[i].centre[1] << ',' << knotsurface[i].centre[2] << ") , (" << knotsurface[i].normal[0] << ',' << knotsurface[i].normal[1] << ',' << knotsurface[i].normal[2] << ") \n";

		if(getline(knotin,buff)) temp = buff;   //read in "outer loop"
		if(getline(knotin,buff)) temp = buff;   //read in "outer loop"

		i++;
	}

	NK = i;
	/* Work out space scaling for knot surface */
	double scale[3];
	if(maxxin-minxin>0) scale[0] = xmax/(maxxin-minxin);
	else scale[0] = 1;
	if(maxyin-minyin>0) scale[1] = ymax/(maxyin-minyin);
	else scale[1] = 1;
	if(maxzin-minzin>0) scale[2] = zmax/(maxzin-minzin);
	else scale[2] = 1;
	double midpoint[3];
	double norm;
	//double p1x,p1y,p1z,p2x,p2y,p2z,nx,ny,nz;
	midpoint[0] = 0.5*(maxxin+minxin);
	midpoint[1] = 0.5*(maxyin+minyin);
	midpoint[2] = 0.5*(maxzin+minzin);

	/*Rescale points and normals to fit grid properly*/
	for(i=0;i<NK;i++)
	{
		for(j=0;j<3;j++)
		{
			knotsurface[i].xvertex[j] = scale[0]*(knotsurface[i].xvertex[j] - midpoint[0]);
			knotsurface[i].yvertex[j] = scale[1]*(knotsurface[i].yvertex[j] - midpoint[1]);
			knotsurface[i].zvertex[j] = scale[2]*(knotsurface[i].zvertex[j] - midpoint[2]);
			knotsurface[i].centre[j] = scale[j]*(knotsurface[i].centre[j] - midpoint[j]);
		}

		norm = sqrt(scale[1]*scale[1]*scale[2]*scale[2]*knotsurface[i].normal[0]*knotsurface[i].normal[0] +
				scale[0]*scale[0]*scale[2]*scale[2]*knotsurface[i].normal[1]*knotsurface[i].normal[1] +
				scale[0]*scale[0]*scale[1]*scale[1]*knotsurface[i].normal[2]*knotsurface[i].normal[2]);

		knotsurface[i].normal[0] *= scale[1]*scale[2]/norm;
		knotsurface[i].normal[1] *= scale[0]*scale[2]/norm;
		knotsurface[i].normal[2] *= scale[0]*scale[1]/norm;

		/*Check surface normal is correct
		  p1x = knotsurface[i].xvertex[1] - knotsurface[i].xvertex[0];
		  p1y = knotsurface[i].yvertex[1] - knotsurface[i].yvertex[0];
		  p1z = knotsurface[i].zvertex[1] - knotsurface[i].zvertex[0];
		  p2x = knotsurface[i].xvertex[2] - knotsurface[i].xvertex[0];
		  p2y = knotsurface[i].yvertex[2] - knotsurface[i].yvertex[0];
		  p2z = knotsurface[i].zvertex[2] - knotsurface[i].zvertex[0];
		  nx = p1y*p2z - p2y*p1z;
		  ny = p1z*p2x - p2z*p1x;
		  nz = p1x*p2y - p2x*p1y;
		  norm = sqrt(nx*nx+ny*ny+nz*nz);
		  nx = nx/norm;
		  ny = ny/norm;
		  nz = nz/norm;
		  cout << nx*knotsurface[i].normal[0] + ny*knotsurface[i].normal[1] + nz*knotsurface[i].normal[2] << '\n';
		 */

		r10 = sqrt((knotsurface[i].xvertex[1]-knotsurface[i].xvertex[0])*(knotsurface[i].xvertex[1]-knotsurface[i].xvertex[0]) + (knotsurface[i].yvertex[1]-knotsurface[i].yvertex[0])*(knotsurface[i].yvertex[1]-knotsurface[i].yvertex[0]) + (knotsurface[i].zvertex[1]-knotsurface[i].zvertex[0])*(knotsurface[i].zvertex[1]-knotsurface[i].zvertex[0]));
		r20 = sqrt((knotsurface[i].xvertex[2]-knotsurface[i].xvertex[0])*(knotsurface[i].xvertex[2]-knotsurface[i].xvertex[0]) + (knotsurface[i].yvertex[2]-knotsurface[i].yvertex[0])*(knotsurface[i].yvertex[2]-knotsurface[i].yvertex[0]) + (knotsurface[i].zvertex[2]-knotsurface[i].zvertex[0])*(knotsurface[i].zvertex[2]-knotsurface[i].zvertex[0]));
		r21 = sqrt((knotsurface[i].xvertex[2]-knotsurface[i].xvertex[1])*(knotsurface[i].xvertex[2]-knotsurface[i].xvertex[1]) + (knotsurface[i].yvertex[2]-knotsurface[i].yvertex[1])*(knotsurface[i].yvertex[2]-knotsurface[i].yvertex[1]) + (knotsurface[i].zvertex[2]-knotsurface[i].zvertex[1])*(knotsurface[i].zvertex[2]-knotsurface[i].zvertex[1]));
		s = (r10+r20+r21)/2;
		knotsurface[i].area = sqrt(s*(s-r10)*(s-r20)*(s-r21));
		A += knotsurface[i].area;
	}

	cout << "Input scaled by: " << scale[0] << ' ' << scale[1] << ' ' << scale[2] << "in x,y and z\n";

	return A;

}

/*************************Functions for B and Phi calcs*****************************/

void initial_cond(double *x, double *y, double *z, double *phi)
{
	cout << "Calculating scalar potential...\n";
	time_t then = time(NULL);
	phi_calc(x,y,z,phi);
	time_t now = time(NULL);
	cout << "Initialisation took " << now - then << " seconds.\n";
	cout << "Printing B and phi...\n";
	print_B_phi(x, y, z, phi);
}

void phi_calc(double *x, double *y, double *z, double *phi)
{
	int i,j,k,n,s;
	double rx,ry,rz,r;


#pragma omp parallel default(none) shared ( x, y, z, knotsurface, phi, NK ) private ( i, j, k, n, s, rx, ry, rz , r)
	{
#pragma omp for
		for(i=0;i<Nx;i++)
		{
			for(j=0; j<Ny; j++)
			{
				for(k=0; k<Nz; k++)
				{
					n = pt(i,j,k);
					phi[n] = 0;
					for(s=0;s<NK;s++)
					{
						rx = knotsurface[s].centre[0]-x[i];
						ry = knotsurface[s].centre[1]-y[j];
						rz = knotsurface[s].centre[2]-z[k];
						r = sqrt(rx*rx+ry*ry+rz*rz);
						if(r>0) phi[n] += (rx*knotsurface[s].normal[0] + ry*knotsurface[s].normal[1] + rz*knotsurface[s].normal[2])*knotsurface[s].area/(2*r*r*r);
					}
					while(phi[n]>M_PI) phi[n] -= 2*M_PI;
					while(phi[n]<-M_PI) phi[n] += 2*M_PI;
				}
			}
		}
	}

}

/*************************Functions for FN dynamics*****************************/

void uv_initialise(double *phi, double *u, double *v)
{
	int n;

	for(n=0; n<Nx*Ny*Nz; n++)
	{
		u[n] = (2*cos(phi[n]) - 0.4);
		v[n] = (sin(phi[n]) - 0.4);
		//if missed set value to -0.4 (vortex centre value)
	}
}

void crossgrad_calc(double *x, double *y, double *z, double *u, double *v, double *ucvx, double *ucvy, double *ucvz)
{
	int i,j,k,n,kup,kdwn;
	double dxu,dyu,dzu,dxv,dyv,dzv,ucvmag,ucvmax;
	knotcurve.push_back(knotpoint());
	ucvmax = -1.0; // should always be +ve, so setting it to an initially -ve # means it always gets written to once.  
	for(i=0;i<Nx;i++)
	{
		for(j=0; j<Ny; j++)
		{
			for(k=0; k<Nz; k++)   //Central difference
			{
				if(periodic)   //check for periodic boundaries
				{
					kup = incp(k,1,Nz);
					kdwn = incp(k,-1,Nz);
				}
				else
				{
					kup = incw(k,1,Nz);
					kdwn = incw(k,-1,Nz);
				}
				dxu = 0.5*(u[pt(incw(i,1,Nx),j,k)]-u[pt(incw(i,-1,Nx),j,k)])/h;
				dxv = 0.5*(v[pt(incw(i,1,Nx),j,k)]-v[pt(incw(i,-1,Nx),j,k)])/h;
				dyu = 0.5*(u[pt(i,incw(j,1,Ny),k)]-u[pt(i,incw(j,-1,Ny),k)])/h;
				dyv = 0.5*(v[pt(i,incw(j,1,Ny),k)]-v[pt(i,incw(j,-1,Ny),k)])/h;
				dzu = 0.5*(u[pt(i,j,kup)]-u[pt(i,j,kdwn)])/h;
				dzv = 0.5*(v[pt(i,j,kup)]-v[pt(i,j,kdwn)])/h;
				n = pt(i,j,k);
				ucvx[n] = dyu*dzv - dzu*dyv;
				ucvy[n] = dzu*dxv - dxu*dzv;    //Grad u cross Grad v
				ucvz[n] = dxu*dyv - dyu*dxv;
				ucvmag = sqrt(ucvx[n]*ucvx[n] + ucvy[n]*ucvy[n] + ucvz[n]*ucvz[n]);
				if(ucvmag > ucvmax)
				{
					ucvmax = ucvmag;
					knotcurve[0].xcoord=x[i];
					knotcurve[0].ycoord=y[j];
					knotcurve[0].zcoord=z[k];
				}
			}
		}
	}
	if(ucvmax<0.1) knotexists = false;
	else knotexists = true;
}

void find_knot_properties(double *x, double *y, double *z, double *ucvx, double *ucvy, double *ucvz, double *u, double t,  gsl_multimin_fdfminimizer* minimizerstate)
{
	int i,j,k,idwn,jdwn,kdwn,m,pts,iinc,jinc,kinc,attempts;
	int s=1;
	bool finish=false;
	double ucvxs, ucvys, ucvzs, graducvx, graducvy, graducvz, prefactor, xd, yd ,zd, norm, fx, fy, fz, xdiff, ydiff, zdiff;

	bool burnin = false; 

	/*calculate local direction of grad u x grad v (the tangent to the knot curve) at point s-1, then move to point s by moving along tangent + unit confinement force*/
	while (finish==false)
	{

		/**Find nearest gridpoint**/
		idwn = (int) ((knotcurve[s-1].xcoord/h) - 0.5 + Nx/2.0);
		jdwn = (int) ((knotcurve[s-1].ycoord/h) - 0.5 + Ny/2.0);
		kdwn = (int) ((knotcurve[s-1].zcoord/h) - 0.5 + Nz/2.0);
		if(idwn<0 || jdwn<0 || kdwn<0 || idwn > Nx-1 || jdwn > Ny-1 || kdwn > Nz-1) break;
		pts=0;
		ucvxs=0;
		ucvys=0;
		ucvzs=0;
		graducvx=0;
		graducvy=0;
		graducvz=0;
		/*curve to gridpoint down distance*/
		xd = (knotcurve[s-1].xcoord - x[idwn])/h;
		yd = (knotcurve[s-1].ycoord - y[jdwn])/h;
		zd = (knotcurve[s-1].zcoord - z[kdwn])/h;
		for(m=0;m<8;m++)  //linear interpolation from 8 nearest neighbours
		{
			/* Work out increments*/
			iinc = m%2;
			jinc = (m/2)%2;
			kinc = (m/4)%2;
			/*Loop over nearest points*/
			i = incw(idwn, iinc, Nx);
			j = incw(jdwn, jinc, Ny);
			if(periodic) k = incp(kdwn,kinc, Nz);
			else k = incw(kdwn,kinc, Nz);
			prefactor = (1-iinc + pow(-1,1+iinc)*xd)*(1-jinc + pow(-1,1+jinc)*yd)*(1-kinc + pow(-1,1+kinc)*zd);
			/*interpolate grad u x grad v over nearest points*/
			ucvxs += prefactor*ucvx[pt(i,j,k)];
			ucvys += prefactor*ucvy[pt(i,j,k)];
			ucvzs += prefactor*ucvz[pt(i,j,k)];
		}
		norm = sqrt(ucvxs*ucvxs + ucvys*ucvys + ucvzs*ucvzs);
		//cout << "Norm after attempt " << attempts-1 << ": " << norm << '\n';
		ucvxs = ucvxs/norm; //normalise
		ucvys = ucvys/norm; //normalise
		ucvzs = ucvzs/norm; //normalise
		double testx = knotcurve[s-1].xcoord + 2*ucvxs*lambda/(32*M_PI);
		double testy = knotcurve[s-1].ycoord + 2*ucvys*lambda/(32*M_PI);
		double testz = knotcurve[s-1].zcoord + 2*ucvzs*lambda/(32*M_PI);
		knotcurve.push_back(knotpoint());
		// ok, lets set up the minimizer

		gsl_vector* v = gsl_vector_alloc (3);
		gsl_vector_set (v, 0, testx);
		gsl_vector_set (v, 1, testy);
		gsl_vector_set (v, 2, testz);

		double* par[6];
		par[0] = x; par[1] = y; par[2] = z; par[3] = ucvx; par[4] = ucvy; par[5] = ucvz;
		gsl_multimin_function_fdf my_func;
		my_func.n = 3;
		my_func.f = &my_f;
		my_func.df =&my_df;
		my_func.fdf = &my_fdf;
		my_func.params = (void*) par;

		gsl_multimin_fdfminimizer_set(minimizerstate, &my_func, v, 0.001, 1e-4);

		int iter = 0;
		int status = 0;
		do
		{
			iter++;
			status = gsl_multimin_fdfminimizer_iterate (minimizerstate);
			if (status) break;

			status = gsl_multimin_test_gradient (minimizerstate->gradient, 1e-3);
		}
		while (status == GSL_CONTINUE && iter < 100);

		knotcurve[s].xcoord = gsl_vector_get (minimizerstate->x, 0);
		knotcurve[s].ycoord = gsl_vector_get (minimizerstate->x, 1);
		knotcurve[s].zcoord = gsl_vector_get (minimizerstate->x, 2);
		
		knotcurve[s].xcoord= knotcurve[s-1].xcoord + 0.5*ucvxs*lambda/(32*M_PI);
		knotcurve[s].ycoord = knotcurve[s-1].ycoord + 0.5*ucvys*lambda/(32*M_PI);
		knotcurve[s].zcoord = knotcurve[s-1].zcoord + 0.5*ucvzs*lambda/(32*M_PI);

		gsl_vector_free (v);


		//~ if(burnin==false && s==300)
		//~ {
			//~ knotcurve.erase(knotcurve.begin(),knotcurve.begin()+s);
			//~ s = 0;
			//~ burnin=true;
		//~ }

		xdiff = knotcurve[0].xcoord - knotcurve[s].xcoord;     //distance from start/end point
		ydiff = knotcurve[0].ycoord - knotcurve[s].ycoord;
		zdiff = knotcurve[0].zcoord - knotcurve[s].zcoord;
		if(sqrt(xdiff*xdiff + ydiff*ydiff + zdiff*zdiff) < lambda/(2*M_PI) && s > 32) finish = true;
		if(s>50000) finish = true;
		s++;
	}

	/*Fill in remaining points*/
	double dx = xdiff/16.0;
	double dy = ydiff/16.0;
	double dz = zdiff/16.0;
	for(m=0; m<15; m++)
	{
		knotcurve.push_back(knotpoint());
		knotcurve[s+m].xcoord = knotcurve[s+m-1].xcoord + dx;
		knotcurve[s+m].ycoord = knotcurve[s+m-1].ycoord + dy;
		knotcurve[s+m].zcoord = knotcurve[s+m-1].zcoord + dz;
	}

	int NP = knotcurve.size();  //store number of points in knot curve

	/*******Erase some points********/

	//~ for(s=0; s<NP%8; s++) knotcurve.pop_back();    //delete last couple of elements
	//~ for(s=0; s<NP/8; s++)                          //delete 7 of every 8 elements
	//~ {
	//~ knotcurve.erase(knotcurve.end()-s-8,knotcurve.end()-s-1);
	//~ }

	/********************************/

	NP = knotcurve.size();  //update number of points in knot curve

	/*******Vertex averaging*********/

	double totlength, dl;
	for(i=0;i<3;i++)   //repeat a couple of times because of end point
	{
		totlength=0;
		for(s=0; s<NP; s++)   //Work out total length of curve
		{
			dx = knotcurve[incp(s,1,NP)].xcoord - knotcurve[s].xcoord;
			dy = knotcurve[incp(s,1,NP)].ycoord - knotcurve[s].ycoord;
			dz = knotcurve[incp(s,1,NP)].zcoord - knotcurve[s].zcoord;
			totlength += sqrt(dx*dx + dy*dy + dz*dz);
		}
		dl = totlength/NP;
		for(s=0; s<NP; s++)    //Move points to have spacing dl
		{
			dx = knotcurve[incp(s,1,NP)].xcoord - knotcurve[s].xcoord;
			dy = knotcurve[incp(s,1,NP)].ycoord - knotcurve[s].ycoord;
			dz = knotcurve[incp(s,1,NP)].zcoord - knotcurve[s].zcoord;
			norm = sqrt(dx*dx + dy*dy + dz*dz);
			knotcurve[incp(s,1,NP)].xcoord = knotcurve[s].xcoord + dl*dx/norm;
			knotcurve[incp(s,1,NP)].ycoord = knotcurve[s].ycoord + dl*dy/norm;
			knotcurve[incp(s,1,NP)].zcoord = knotcurve[s].zcoord + dl*dz/norm;
		}
	}

	/********************************/

	/*********Naive FT doesn't work**********/

	/*double *kxp, *kyp, *kzp, *kxpi, *kypi, *kzpi;
	  int kmax;

	  if(0.5*lambda/M_PI > totlength/NP)
	  {
	  kmax = ((int) (totlength*2*M_PI/lambda));  //Ignore oscillations of higher freq than 2pi/core diameter
	  cout << "kmax: " << kmax << " NP: " << NP << '\n';
	  kxp = new double [NP];
	  kyp = new double [NP];
	  kzp = new double [NP];
	  kxpi = new double [NP];
	  kypi = new double [NP];
	  kzpi = new double [NP];
	  for(k=0; k<kmax; k++)
	  {
	  kxp[k] = 0;
	  kxpi[k] = 0;
	  kyp[k] = 0;
	  kypi[k] = 0;
	  kzp[k] = 0;
	  kzpi[k] = 0;
	  for(s=0; s<NP; s++)
	  {
	  kxp[k] += knotcurve[s].xcoord*cos(2*M_PI*s*k/NP);
	  kxpi[k] += knotcurve[s].xcoord*sin(2*M_PI*s*k/NP);
	  kyp[k] += knotcurve[s].ycoord*cos(2*M_PI*s*k/NP);
	  kypi[k] += knotcurve[s].ycoord*sin(2*M_PI*s*k/NP);
	  kzp[k] += knotcurve[s].zcoord*cos(2*M_PI*s*k/NP);
	  kzpi[k] += knotcurve[s].zcoord*sin(2*M_PI*s*k/NP);
	  }
	  }

	  double px, py, pz;

	  for(s=0; s<NP; s++)
	  {
	  px = 0;
	  py = 0;
	  pz = 0;
	  for(k=0; k<kmax; k++)
	  {
	  px += (kxp[k]*cos(2*M_PI*s*k/NP) + kxpi[k]*sin(2*M_PI*s*k/NP))/kmax;
	  py += (kyp[k]*cos(2*M_PI*s*k/NP) + kypi[k]*sin(2*M_PI*s*k/NP))/kmax;
	  pz += (kzp[k]*cos(2*M_PI*s*k/NP) + kzpi[k]*sin(2*M_PI*s*k/NP))/kmax;
	  }
	  knotcurve[s].xcoord = px;
	  knotcurve[s].ycoord = py;
	  knotcurve[s].zcoord = pz;
	  }

	  cout << knotcurve.size() << '\n';

	  delete [] kxp;
	  delete [] kyp;
	  delete [] kzp;
	  delete [] kxpi;
	  delete [] kypi;
	  delete [] kzpi;
	  }/*

	/********Fourier transform******/   //may not be necessary
	//~ /********Fourier transform******/
	//~ NP = knotcurve.size();  //update number of points in knot curve
	//~ vector<double> coord(NP);
	//~ gsl_fft_real_wavetable * real;
	//~ gsl_fft_halfcomplex_wavetable * hc;
	//~ gsl_fft_real_workspace * work;
	//~ work = gsl_fft_real_workspace_alloc (NP);
	//~ real = gsl_fft_real_wavetable_alloc (NP);
	//~ hc = gsl_fft_halfcomplex_wavetable_alloc (NP);

	//~ for(int j=1; j<4; j++)
	//~ {
	//~ switch(j)
	//~ {
	//~ case 1 :
	//~ for(int i=0; i<NP; i++) coord[i] =  knotcurve[i].xcoord ; break;
	//~ case 2 :
	//~ for(int i=0; i<NP; i++) coord[i] =  knotcurve[i].ycoord ; break;
	//~ case 3 :
	//~ for(int i=0; i<NP; i++) coord[i] =  knotcurve[i].zcoord ; break;
	//~ }
	//~ double* data = coord.data();

	//~ // take the fft
	//~ gsl_fft_real_transform (data, 1, NP, real, work);

	//~ // 21/11/2016: make our low pass filter. To apply our filter. we should sample frequencies fn = n/Delta N , n = -N/2 ... N/2
	//~ // this is discretizing the nyquist interval, with extreme frequency ~1/2Delta.
	//~ // to cut out the frequencies of grid fluctuation size and larger we need a lengthscale Delta to
	//~ // plug in above. im doing a rough length calc below, this might be overkill.
	//~ // at the moment its just a hard filter, we can choose others though.

	//~ // compute a rough length to set scale
	//~ int roughlength =0;
	//~ for (int  s =1 ; s<NP ; s++) roughlength += sqrt((knotcurve[s].xcoord - knotcurve[s-1].xcoord)*(knotcurve[s].xcoord - knotcurve[s-1].xcoord)+ (knotcurve[s].ycoord - knotcurve[s-1].ycoord)*(knotcurve[s].ycoord - knotcurve[s-1].ycoord) + (knotcurve[s].zcoord - knotcurve[s-1].zcoord)*(knotcurve[s].zcoord - knotcurve[s-1].zcoord));

	//~ double filter;
	//~ for (int i = 0; i < NP; ++i)
	//~ {
	//~ if (i < roughlength/h)
	//~ {
	//~ // passband
	//~ filter = 1.0;
	//~ }
	//~ else
	//~ {
	//~ // stopband
	//~ filter = 0.0;
	//~ }
	//~ data[i] *= filter;
	//~ };

	//~ // transform back
	//~ gsl_fft_halfcomplex_inverse (data, 1, NP, hc, work);
	//~ switch(j)
	//~ {
	//~ case 1 :
	//~ for(int i=0; i<NP; i++)  knotcurve[i].xcoord = coord[i] ; break;
	//~ case 2 :
	//~ for(int i=0; i<NP; i++)  knotcurve[i].ycoord = coord[i] ; break;
	//~ case 3 :
	//~ for(int i=0; i<NP; i++)  knotcurve[i].zcoord = coord[i] ; break;
	//~ }

	//~ }
	//~ gsl_fft_real_wavetable_free (real);
	//~ gsl_fft_halfcomplex_wavetable_free (hc);
	//~ gsl_fft_real_workspace_free (work);
	/*******************************/

	/*******************************/

	/*****Writhe and twist integrals******/
	NP = knotcurve.size();  //store number of points in knot curve
	//if(t==50) cout << "Number of points: " << NP << '\n';
	double totwrithe = 0;
	double tottwist = 0;
	double ds = 2*M_PI/NP;
	double dxds, dyds, dzds, dxdm, dydm, dzdm, dxu, dyu, dzu, dxup, dyup, dzup, bx, by, bz, check;
	totlength = 0;

	/******************Interpolate direction of grad u for twist calc*******/
	/**Find nearest gridpoint**/
	for(s=0; s<NP; s++)
	{
		idwn = (int) ((knotcurve[s].xcoord/h) - 0.5 + Nx/2.0);
		jdwn = (int) ((knotcurve[s].ycoord/h) - 0.5 + Ny/2.0);
		kdwn = (int) ((knotcurve[s].zcoord/h) - 0.5 + Nz/2.0);
		if(idwn<0 || jdwn<0 || kdwn<0 || idwn > Nx-1 || jdwn > Ny-1 || kdwn > Nz-1) break;
		dxu=0;
		dyu=0;
		dzu=0;
		/*curve to gridpoint down distance*/
		xd = (knotcurve[s].xcoord - x[idwn])/h;
		yd = (knotcurve[s].ycoord - y[jdwn])/h;
		zd = (knotcurve[s].zcoord - z[kdwn])/h;
		for(m=0;m<8;m++)  //linear interpolation of 8 NNs
		{
			/* Work out increments*/
			iinc = m%2;
			jinc = (m/2)%2;
			kinc = (m/4)%2;
			/*Loop over nearest points*/
			i = incw(idwn, iinc, Nx);
			j = incw(jdwn, jinc, Ny);
			if(periodic) k = incp(kdwn,kinc, Nz);
			else k = incw(kdwn,kinc, Nz);
			prefactor = (1-iinc + pow(-1,1+iinc)*xd)*(1-jinc + pow(-1,1+jinc)*yd)*(1-kinc + pow(-1,1+kinc)*zd);   //terms of the form (1-xd)(1-yd)zd etc. (interpolation coefficient)
			/*interpolate grad u over nearest points*/
			dxu += prefactor*0.5*(u[pt(incw(i,1,Nx),j,k)] -  u[pt(incw(i,-1,Nx),j,k)])/h;  //central diff
			dyu += prefactor*0.5*(u[pt(i,incw(j,1,Ny),k)] -  u[pt(i,incw(j,-1,Ny),k)])/h;
			if(periodic) prefactor*0.5*(u[pt(i,j,incp(k,1,Nz))] -  u[pt(i,j,incp(k,-1,Nz))])/h;
			else  dzu += prefactor*0.5*(u[pt(i,j,incw(k,1,Nz))] -  u[pt(i,j,incw(k,-1,Nz))])/h;
		}
		//project du onto perp of tangent direction first
		dx = 0.5*(knotcurve[incp(s,1,NP)].xcoord - knotcurve[incp(s,-1,NP)].xcoord);   //central diff as a is defined on the points
		dy = 0.5*(knotcurve[incp(s,1,NP)].ycoord - knotcurve[incp(s,-1,NP)].ycoord);
		dz = 0.5*(knotcurve[incp(s,1,NP)].zcoord - knotcurve[incp(s,-1,NP)].zcoord);
		dxup = dxu - (dxu*dx + dyu*dy + dzu*dz)*dx/(dx*dx+dy*dy+dz*dz);               //Grad u_j * (delta_ij - t_i t_j)
		dyup = dyu - (dxu*dx + dyu*dy + dzu*dz)*dy/(dx*dx+dy*dy+dz*dz);
		dzup = dzu - (dxu*dx + dyu*dy + dzu*dz)*dz/(dx*dx+dy*dy+dz*dz);
		/*Vector a is the normalised gradient of u, should point in direction of max u perp to t*/
		norm = sqrt(dxup*dxup+dyup*dyup+dzup*dzup);
		knotcurve[s].ax = dxup/norm;
		knotcurve[s].ay = dyup/norm;
		knotcurve[s].az = dzup/norm;
	}

	/***Do the integrals**/
	for(s=0; s<NP; s++)    //fwd diff (defined on connecting line) (cell data in paraview)
	{
		dxds = (knotcurve[incp(s,1,NP)].xcoord - knotcurve[s].xcoord)/(ds);
		dyds = (knotcurve[incp(s,1,NP)].ycoord - knotcurve[s].ycoord)/(ds);
		dzds = (knotcurve[incp(s,1,NP)].zcoord - knotcurve[s].zcoord)/(ds);
		/*These quantities defined on line connecting points s and s+1*/
		knotcurve[s].writhe = 0;
		knotcurve[s].length = sqrt(dxds*dxds + dyds*dyds + dzds*dzds)*ds;  //actual length of thing
		bx = (knotcurve[incp(s,1,NP)].ax - knotcurve[s].ax)/ds;
		by = (knotcurve[incp(s,1,NP)].ay - knotcurve[s].ay)/ds;
		bz = (knotcurve[incp(s,1,NP)].az - knotcurve[s].az)/ds;
		knotcurve[s].twist = (dxds*(knotcurve[s].ay*bz - knotcurve[s].az*by) + dyds*(knotcurve[s].az*bx - knotcurve[s].ax*bz) + dzds*(knotcurve[s].ax*by - knotcurve[s].ay*bx))/(2*M_PI*sqrt(dxds*dxds + dyds*dyds + dzds*dzds));
		/*Check this is actually normal to tangent*/
		/*check = fabs(0.5*(knotcurve[s].ax + knotcurve[incp(s,1,NP)].ax)*dxds + 0.5*(knotcurve[s].ay + knotcurve[incp(s,1,NP)].ay)*dyds + 0.5*(knotcurve[s].az + knotcurve[incp(s,1,NP)].az)*dzds)/sqrt(dxds*dxds + dyds*dyds + dzds*dzds);
		  if(check>0.01) cout << s << ": (" << knotcurve[s].xcoord << ", " << knotcurve[s].ycoord << ", " << knotcurve[s].zcoord << "). Grad u . t = " << check << '\n';*/
		for(m=0; m<NP; m++)
		{
			if(s != m)
			{
				xdiff = 0.5*(knotcurve[incp(s,1,NP)].xcoord + knotcurve[s].xcoord - knotcurve[incp(m,1,NP)].xcoord - knotcurve[m].xcoord);   //interpolate, consistent with fwd diff
				ydiff = 0.5*(knotcurve[incp(s,1,NP)].ycoord + knotcurve[s].ycoord - knotcurve[incp(m,1,NP)].ycoord - knotcurve[m].ycoord);
				zdiff = 0.5*(knotcurve[incp(s,1,NP)].zcoord + knotcurve[s].zcoord - knotcurve[incp(m,1,NP)].zcoord - knotcurve[m].zcoord);
				dxdm = (knotcurve[incp(m,1,NP)].xcoord - knotcurve[m].xcoord)/(ds);
				dydm = (knotcurve[incp(m,1,NP)].ycoord - knotcurve[m].ycoord)/(ds);
				dzdm = (knotcurve[incp(m,1,NP)].zcoord - knotcurve[m].zcoord)/(ds);
				knotcurve[s].writhe += ds*(xdiff*(dyds*dzdm - dzds*dydm) + ydiff*(dzds*dxdm - dxds*dzdm) + zdiff*(dxds*dydm - dyds*dxdm))/(4*M_PI*(xdiff*xdiff + ydiff*ydiff + zdiff*zdiff)*sqrt(xdiff*xdiff + ydiff*ydiff + zdiff*zdiff));
			}
		}
		/*Add on writhe, twist and length*/
		totwrithe += knotcurve[s].writhe*ds;
		totlength += knotcurve[s].length;
		tottwist  += knotcurve[s].twist*ds;
	}

	/***Write values to file*******/
	ofstream wrout;
	wrout.open("writhe.txt",ios_base::app);
	wrout << t << '\t' << totwrithe << '\t' << tottwist << '\t' << totlength << '\n';
	wrout.close();
}

void uv_update(double *u, double *v, double *ku, double *kv, double *kut, double *kvt, double *uold, double *vold)
{
	int i,j,k,l,n,kup,kdwn;
	double D2u;

#pragma omp for
	for(i=0;i<Nx;i++)
	{
		for(j=0; j<Ny; j++)
		{
			for(k=0; k<Nz; k++)
			{
				n = pt(i,j,k);
				uold[n] = u[n];  //old value of u
				vold[n] = v[n];  //old value of v
				kut[n] = 0;
				kvt[n] = 0;
			}
		}
	}

	for(l=0;l<4;l++)  //u and v update for each fractional time step
	{
#pragma omp for
		for(i=0;i<Nx;i++)
		{
			for(j=0; j<Ny; j++)
			{
				for(k=0; k<Nz; k++)   //Central difference
				{
					n = pt(i,j,k);
					if(periodic)   //check for periodic boundaries
					{
						kup = incp(k,1,Nz);
						kdwn = incp(k,-1,Nz);
					}
					else
					{
						kup = incw(k,1,Nz);
						kdwn = incw(k,-1,Nz);
					}
					D2u = oneoverhsq*(u[pt(incw(i,1,Nx),j,k)] + u[pt(incw(i,-1,Nx),j,k)] + u[pt(i,incw(j,1,Ny),k)] + u[pt(i,incw(j,-1,Ny),k)] + u[pt(i,j,kup)] + u[pt(i,j,kdwn)] - 6.0*u[n]);
					ku[n] = oneoverepsilon*(u[n] - u[n]*u[n]*u[n]/3.0 - v[n]) + D2u;
					kv[n] = epsilon*(u[n] + beta - gam*v[n]);
				}
			}
		}

		switch (l)
		{
			case 0:
				{
					uv_add(u,v,uold,vold,ku,kv,kut,kvt,0.5,1.0);   //add k1 to uv and add to total k
				}
				break;

			case 1:
				{
					uv_add(u,v,uold,vold,ku,kv,kut,kvt,0.5,2.0);   //add k2 to uv and add to total k
				}
				break;

			case 2:
				{
					uv_add(u,v,uold,vold,ku,kv,kut,kvt,1.0,2.0);      //add k3 to uv and add to total k
				}
				break;

			case 3:
				{
#pragma omp for
					for(i=0;i<Nx;i++)
					{
						for(j=0; j<Ny; j++)
						{
							for(k=0; k<Nz; k++)  //update
							{
								n = pt(i,j,k);
								u[n] = uold[n] + dtime*sixth*(kut[n]+ku[n]);
								v[n] = vold[n] + dtime*sixth*(kvt[n]+kv[n]);
							}
						}
					}
				}
				break;

			default:
				break;
		}
	}
}

void uv_add(double *u, double *v, double* uold, double *vold, double *ku, double *kv, double *kut, double *kvt, double inc, double coeff)
{
	int i,j,k,n;

#pragma omp for
	for(i=0;i<Nx;i++)
	{
		for(j=0; j<Ny; j++)
		{
			for(k=0; k<Nz; k++)  //update
			{
				n = pt(i,j,k);
				u[n] = uold[n] + dtime*inc*ku[n];
				v[n] = vold[n] + dtime*inc*kv[n];
				kut[n] += coeff*ku[n];
				kvt[n] += coeff*kv[n];
			}
		}
	}

}

void uv_update_euler(double *u, double *v, double *D2u)
{
	int i,j,k,l,n,kup,kdwn;

#pragma omp for
	for(i=0;i<Nx;i++)
	{
		for(j=0; j<Ny; j++)
		{
			for(k=0; k<Nz; k++)
			{
				n = pt(i,j,k);
				if(periodic)   //check for periodic boundaries
				{
					kup = incp(k,1,Nz);
					kdwn = incp(k,-1,Nz);
				}
				else
				{
					kup = incw(k,1,Nz);
					kdwn = incw(k,-1,Nz);
				}
				D2u[n] = (u[pt(incw(i,1,Nx),j,k)] + u[pt(incw(i,-1,Nx),j,k)] + u[pt(i,incw(j,1,Ny),k)] + u[pt(i,incw(j,-1,Ny),k)] + u[pt(i,j,kup)] + u[pt(i,j,kdwn)] - 6*u[n])/(h*h);
			}
		}
	}

#pragma omp for
	for(i=0;i<Nx;i++)
	{
		for(j=0; j<Ny; j++)
		{
			for(k=0; k<Nz; k++)
			{
				n = pt(i,j,k);
				u[n] = u[n] + dtime*((u[n] - u[n]*u[n]*u[n]/3 - v[n])/epsilon + D2u[n]);
				v[n] = v[n] + dtime*(epsilon*(u[n] + beta - gam*v[n]));
			}
		}
	}
}

/*************************File reading and writing*****************************/

void print_uv(double *x, double *y, double *z, double *u, double *v, double *ucvx, double *ucvy, double *ucvz, double t)
{
	int i,j,k,n;
	stringstream ss;
	ss << "uv_plot" << t << ".vtk";
	ofstream uvout (ss.str().c_str());

	uvout << "# vtk DataFile Version 3.0\nUV fields\nASCII\nDATASET STRUCTURED_POINTS\n";
	uvout << "DIMENSIONS " << Nx << ' ' << Ny << ' ' << Nz << '\n';
	uvout << "ORIGIN " << x[0] << ' ' << y[0] << ' ' << z[0] << '\n';
	uvout << "SPACING " << h << ' ' << h << ' ' << h << '\n';
	uvout << "POINT_DATA " << Nx*Ny*Nz << '\n';
	uvout << "SCALARS u float\nLOOKUP_TABLE default\n";


	for(k=0; k<Nz; k++)
	{
		for(j=0; j<Ny; j++)
		{
			for(i=0; i<Nx; i++)
			{
				n = pt(i,j,k);
				uvout << u[n] << '\n';
			}
		}
	}

	uvout << "SCALARS v float\nLOOKUP_TABLE default\n";


	for(k=0; k<Nz; k++)
	{
		for(j=0; j<Ny; j++)
		{
			for(i=0; i<Nx; i++)
			{
				n = pt(i,j,k);
				uvout << v[n] << '\n';
			}
		}
	}

	uvout << "SCALARS ucrossv float\nLOOKUP_TABLE default\n";

	for(k=0; k<Nz; k++)
	{
		for(j=0; j<Ny; j++)
		{
			for(i=0; i<Nx; i++)
			{
				n = pt(i,j,k);
				uvout << sqrt(ucvx[n]*ucvx[n] + ucvy[n]*ucvy[n] + ucvz[n]*ucvz[n]) << '\n';
			}
		}
	}

	uvout.close();
}

void print_B_phi(double *x, double *y, double*z, double *phi)
{
	int i,j,k,n;
	string fn = "phi.vtk";

	ofstream Bout (fn.c_str());

	Bout << "# vtk DataFile Version 3.0\nKnot\nASCII\nDATASET STRUCTURED_POINTS\n";
	Bout << "DIMENSIONS " << Nx << ' ' << Ny << ' ' << Nz << '\n';
	Bout << "ORIGIN " << x[0] << ' ' << y[0] << ' ' << z[0] << '\n';
	Bout << "SPACING " << h << ' ' << h << ' ' << h << '\n';
	Bout << "POINT_DATA " << Nx*Ny*Nz << '\n';
	Bout << "SCALARS Phi float\nLOOKUP_TABLE default\n";
	for(k=0; k<Nz; k++)
	{
		for(j=0; j<Ny; j++)
		{
			for(i=0; i<Nx; i++)
			{
				n = pt(i,j,k);
				Bout << phi[n] << '\n';
			}
		}
	}

	Bout.close();
}

void print_info(int Nx, int Ny, int Nz, double dtime, double h, const bool periodic,  int option, string knot_filename, string B_filename)
{
	string fn = "info.txt";

	time_t rawtime;
	struct tm * timeinfo;

	time (&rawtime);
	timeinfo = localtime (&rawtime);

	ofstream infoout (fn.c_str());

	infoout << "run started at\t" << asctime(timeinfo) << "\n";
	infoout << "Number of grid points\t" << Nx << '\t' << Ny << '\t' << Nz << '\n';
	infoout << "timestep\t" << dtime << '\n';
	infoout << "Spacing\t" << h << '\n';
	infoout << "Periodic\t" << periodic << '\n';
	infoout << "initoptions\t" << option << '\n';
	infoout << "knot filename\t" << knot_filename << '\n';
	infoout << "B or uv filename\t" << B_filename << '\n';
	infoout.close();
}

void print_knot(double *x, double *y, double *z, double t)
{
	stringstream ss;
	ss << "knotplot" << t << ".vtk";
	ofstream knotout (ss.str().c_str());

	int i;
	int n = knotcurve.size();

	knotout << "# vtk DataFile Version 3.0\nKnot\nASCII\nDATASET UNSTRUCTURED_GRID\n";
	knotout << "POINTS " << n << " float\n";

	for(i=0; i<n; i++)
	{
		knotout << knotcurve[i].xcoord << ' ' << knotcurve[i].ycoord << ' ' << knotcurve[i].zcoord << '\n';
	}

	knotout << "\n\nCELLS " << n << ' ' << 3*n << '\n';

	for(i=0; i<n; i++)
	{
		knotout << 2 << ' ' << i << ' ' << incp(i,1,n) << '\n';
	}

	knotout << "\n\nCELL_TYPES " << n << '\n';

	for(i=0; i<n; i++)
	{
		knotout << "3\n";
	}

	knotout << "\n\nPOINT_DATA " << n << "\n\n";
	knotout << "\nVECTORS A float\n";
	for(i=0; i<n; i++)
	{
		knotout << knotcurve[i].ax << ' ' << knotcurve[i].ay << ' ' << knotcurve[i].az << '\n';
	}

	knotout << "\n\nCELL_DATA " << n << "\n\n";
	knotout << "\nSCALARS Writhe float\nLOOKUP_TABLE default\n";
	for(i=0; i<n; i++)
	{
		knotout << knotcurve[i].writhe << '\n';
	}

	knotout << "\nSCALARS Twist float\nLOOKUP_TABLE default\n";
	for(i=0; i<n; i++)
	{
		knotout << knotcurve[i].twist << '\n';
	}

	knotout << "\nSCALARS Length float\nLOOKUP_TABLE default\n";
	for(i=0; i<n; i++)
	{
		knotout << knotcurve[i].length << '\n';
	}

	knotout.close();
}

int phi_file_read(double *phi)
{
	string temp,buff;
	stringstream ss;
	ifstream fin (B_filename.c_str());
	int i,j,k,n;

	for(i=0;i<10;i++)
	{
		if(fin.good())
		{
			if(getline(fin,buff)) temp = buff;
		}
		else
		{
			cout << "Something went wrong!\n";
			return 1;
		}
	}

	for(k=0; k<Nz; k++)
	{
		for(j=0; j<Ny; j++)
		{
			for(i=0; i<Nx; i++)
			{
				n=pt(i,j,k);
				ss.clear();
				ss.str("");
				if(fin.good())
				{
					if(getline(fin,buff))
					{
						ss << buff;
						ss >> phi[n];
					}
				}
				else
				{
					cout << "Something went wrong!\n";
					return 1;
				}
			}
		}
	}

	fin.close();

	return 0;
}

int uvfile_read(double *u, double *v)
{
	string temp,buff;
	stringstream ss;
	ifstream fin (B_filename.c_str());
	int i,j,k,n;

	for(i=0;i<10;i++)
	{
		if(fin.good())
		{
			if(getline(fin,buff)) temp = buff;
		}
		else
		{
			cout << "Something went wrong!\n";
			return 1;
		}
	}

	for(k=0; k<Nz; k++)
	{
		for(j=0; j<Ny; j++)
		{
			for(i=0; i<Nx; i++)
			{
				n=pt(i,j,k);
				ss.clear();
				ss.str("");
				if(fin.good())
				{
					if(getline(fin,buff))
					{
						ss << buff;
						ss >> u[n];
					}
				}
				else
				{
					cout << "Something went wrong!\n";
					return 1;
				}
			}
		}
	}

	for(i=0;i<2;i++)
	{
		if(fin.good())
		{
			if(getline(fin,buff)) temp = buff;
		}
		else
		{
			cout << "Something went wrong!\n";
			return 1;
		}
	}

	for(k=0; k<Nz; k++)
	{
		for(j=0; j<Ny; j++)
		{
			for(i=0; i<Nx; i++)
			{
				n=pt(i,j,k);
				ss.clear();
				ss.str("");
				if(fin.good())
				{
					if(getline(fin,buff)) ss << buff;
					ss >> v[n];
				}
				else
				{
					cout << "Something went wrong!\n";
					return 1;
				}
			}
		}
	}

	fin.close();

	return 0;
}


double my_f(const gsl_vector* v, void* params)
{

	int i,j,k,idwn,jdwn,kdwn,m,pts,iinc,jinc,kinc;
	double ucvxs, ucvys, ucvzs,  xd, yd ,zd, xdiff, ydiff, zdiff, prefactor;
	double** parameters = (double**) params;
	double* x= parameters[0];
	double* y= parameters[1];
	double* z= parameters[2];
	double* ucvx= parameters[3];
	double* ucvy= parameters[4];
	double* ucvz= parameters[5];

	double px = gsl_vector_get(v, 0);
	double py = gsl_vector_get(v, 1);
	double pz = gsl_vector_get(v, 2);
	/**Find nearest gridpoint**/
	idwn = (int) ((px/h) - 0.5 + Nx/2.0);
	jdwn = (int) ((py/h) - 0.5 + Ny/2.0);
	kdwn = (int) ((pz/h) - 0.5 + Nz/2.0);
	pts=0;
	ucvxs=0;
	ucvys=0;
	ucvzs=0;
	/*curve to gridpoint down distance*/
	xd = (px - x[idwn])/h;
	yd = (py - y[jdwn])/h;
	zd = (pz - z[kdwn])/h;
	for(m=0;m<8;m++)  //linear interpolation from 8 nearest neighbours
	{
		/* Work out increments*/
		iinc = m%2;
		jinc = (m/2)%2;
		kinc = (m/4)%2;
		/*Loop over nearest points*/
		i = incw(idwn, iinc, Nx);
		j = incw(jdwn, jinc, Ny);
		if(periodic) k = incp(kdwn,kinc, Nz);
		else k = incw(kdwn,kinc, Nz);
		prefactor = (1-iinc + pow(-1,1+iinc)*xd)*(1-jinc + pow(-1,1+jinc)*yd)*(1-kinc + pow(-1,1+kinc)*zd);
		/*interpolate grad u x grad v over nearest points*/
		ucvxs += prefactor*ucvx[pt(i,j,k)];
		ucvys += prefactor*ucvy[pt(i,j,k)];
		ucvzs += prefactor*ucvz[pt(i,j,k)];
	}
	return  - sqrt(ucvxs*ucvxs + ucvys*ucvys + ucvzs*ucvzs);
}

void my_df(const gsl_vector* v, void* params,gsl_vector *df)
{

	double graducvx, graducvy, graducvz, prefactor, xd, yd ,zd, xdiff, ydiff, zdiff;
	int i,j,k,idwn,jdwn,kdwn,m,pts,iinc,jinc,kinc;
	double** parameters = (double**) params;
	double* x= parameters[0];
	double* y= parameters[1];
	double* z= parameters[2];
	double* ucvx= parameters[3];
	double* ucvy= parameters[4];
	double* ucvz= parameters[5];

	double px = gsl_vector_get(v, 0);
	double py = gsl_vector_get(v, 1);
	double pz = gsl_vector_get(v, 2);
	/**Find nearest gridpoint**/
	idwn = (int) ((px/h) - 0.5 + Nx/2.0);
	jdwn = (int) ((py/h) - 0.5 + Ny/2.0);
	kdwn = (int) ((pz/h) - 0.5 + Nz/2.0);
	pts=0;
	/*curve to gridpoint down distance*/
	xd = (px - x[idwn])/h;
	yd = (py - y[jdwn])/h;
	zd = (pz - z[kdwn])/h;
	for(m=0;m<8;m++)
	{
		/* Work out increments*/
		iinc = m%2;
		jinc = (m/2)%2;
		kinc = (m/4)%2;
		/*Loop over nearest points*/
		i = incw(idwn, iinc, Nx);
		j = incw(jdwn, jinc, Ny);
		if(periodic) k = incp(kdwn,kinc, Nz);
		else k = incw(kdwn,kinc, Nz);
		prefactor = (1-iinc + pow(-1,1+iinc)*xd)*(1-jinc + pow(-1,1+jinc)*yd)*(1-kinc + pow(-1,1+kinc)*zd);
		/*interpolate gradients of |grad u x grad v|*/
		graducvx += prefactor*(sqrt(ucvx[pt(incw(i,1,Nx),j,k)]*ucvx[pt(incw(i,1,Nx),j,k)] + ucvy[pt(incw(i,1,Nx),j,k)]*ucvy[pt(incw(i,1,Nx),j,k)] + ucvz[pt(incw(i,1,Nx),j,k)]*ucvz[pt(incw(i,1,Nx),j,k)]) - sqrt(ucvx[pt(incw(i,-1,Nx),j,k)]*ucvx[pt(incw(i,-1,Nx),j,k)] + ucvy[pt(incw(i,-1,Nx),j,k)]*ucvy[pt(incw(i,-1,Nx),j,k)] + ucvz[pt(incw(i,-1,Nx),j,k)]*ucvz[pt(incw(i,-1,Nx),j,k)]))/(2*h);
		graducvy += prefactor*(sqrt(ucvx[pt(i,incw(j,1,Ny),k)]*ucvx[pt(i,incw(j,1,Ny),k)] + ucvy[pt(i,incw(j,1,Ny),k)]*ucvy[pt(i,incw(j,1,Ny),k)] + ucvz[pt(i,incw(j,1,Ny),k)]*ucvz[pt(i,incw(j,1,Ny),k)]) - sqrt(ucvx[pt(i,incw(j,-1,Ny),k)]*ucvx[pt(i,incw(j,-1,Ny),k)] + ucvy[pt(i,incw(j,-1,Ny),k)]*ucvy[pt(i,incw(j,-1,Ny),k)] + ucvz[pt(i,incw(j,-1,Ny),k)]*ucvz[pt(i,incw(j,-1,Ny),k)]))/(2*h);
		if(periodic) graducvz += prefactor*(sqrt(ucvx[pt(i,j,incp(k,1,Nz))]*ucvx[pt(i,j,incp(k,1,Nz))] + ucvy[pt(i,j,incp(k,1,Nz))]*ucvy[pt(i,j,incp(k,1,Nz))] + ucvz[pt(i,j,incp(k,1,Nz))]*ucvz[pt(i,j,incp(k,1,Nz))]) - sqrt(ucvx[pt(i,j,incp(k,-1,Nz))]*ucvx[pt(i,j,incp(k,-1,Nz))] + ucvy[pt(i,j,incp(k,-1,Nz))]*ucvy[pt(i,j,incp(k,-1,Nz))] + ucvz[pt(i,j,incp(k,-1,Nz))]*ucvz[pt(i,j,incp(k,-1,Nz))]))/(2*h);
		else graducvz += prefactor*(sqrt(ucvx[pt(i,j,incw(k,1,Nz))]*ucvx[pt(i,j,incw(k,1,Nz))] + ucvy[pt(i,j,incw(k,1,Nz))]*ucvy[pt(i,j,incw(k,1,Nz))] + ucvz[pt(i,j,incw(k,1,Nz))]*ucvz[pt(i,j,incw(k,1,Nz))]) - sqrt(ucvx[pt(i,j,incw(k,-1,Nz))]*ucvx[pt(i,j,incw(k,-1,Nz))] + ucvy[pt(i,j,incw(k,-1,Nz))]*ucvy[pt(i,j,incw(k,-1,Nz))] + ucvz[pt(i,j,incw(k,-1,Nz))]*ucvz[pt(i,j,incw(k,-1,Nz))]))/(2*h);
	}
	gsl_vector_set(df, 0, - graducvx );
	gsl_vector_set(df, 1,  - graducvy );
	gsl_vector_set(df, 2,  - graducvz );
}

void my_fdf (const gsl_vector *x, void *params, double *f, gsl_vector *df) 
{
	*f = my_f(x, params); 
	my_df(x, params, df);
}
