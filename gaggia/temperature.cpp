#include "temperature.h"
#include <stdlib.h>
#include <string>
#include <fstream>
#include <iostream>
using namespace std;

//-----------------------------------------------------------------------------

Temperature::Temperature()
{
	// attempt to find the sensor path on the filesystem
	m_sensorPath = findSensorPath();
}

//-----------------------------------------------------------------------------

bool Temperature::getDegrees( double *value ) const
{
    // attempt to open the sensor
    ifstream sensor( m_sensorPath.c_str() );

    // read the first line
    string line;
    string temp;
    getline( sensor, line );
    int size = line.length();

    //check is output is good
    while(line.substr(size-3,3) != 'YES'){
      time.sleep(0.2);
      ifstream sensor( m_sensorPath.c_str() ); 
      getline( sensor, line )
    }

    //get temperature line
    getline( sensor, line );
    size = line.length();
    temp = line.substr(size-5,5)

    // convert to degrees
    double degrees = static_cast<double>( atoi(temp.c_str()) ) / 1000.0;

	if ( value != 0 ) *value = degrees;

	return true;
}

//-----------------------------------------------------------------------------

std::string Temperature::findSensorPath()
{
    /* Now we use the TSIC 306, the sensor path is fixed (currently) */
	return "/sys/bus/w1/devices/28-000005e52bb5/w1_slave";
}

//-----------------------------------------------------------------------------
