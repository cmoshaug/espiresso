#include <stdio.h>
#include <sched.h>
#include <ctype.h>
#include <signal.h>
#include <unistd.h>
#include <string>
#include <iostream>
#include <fstream>
#include <map>

#include "timing.h"
#include "regulator.h"
#include "keyboard.h"
#include "inputs.h"
#include "system.h"
#include "display.h"

using namespace std;

//-----------------------------------------------------------------------------

static const string filePath( "/var/log/gaggia/" );
static const string configFile( "/etc/gaggia.conf" );

//-----------------------------------------------------------------------------

std::map<std::string, double> config;

bool g_enableBoiler = true;	///< Enable boiler if true
bool g_quit = false;		///< Should we quit?
bool g_halt = false;        ///< Should we halt? (shutdown the system)

/// Automatic power off time in seconds. Zero disables the time out.
double g_autoPowerOff = 0.0;

Timer g_lastUsed;           ///< When was the last use? (user interaction)

Inputs    g_inputs;         ///< Inputs (buttons)
Regulator g_regulator;      ///< Temperature regulator

//-----------------------------------------------------------------------------

void signalHandler( int signal )
{
	switch ( signal ) {
	case SIGINT:
		printf( "\ngaggia: received SIGINT\n" );
		g_quit = true;
		break;

	case SIGTERM:
		printf( "\ngaggia: received SIGTERM\n" );
		g_quit = true;
		break;
	}
}

//-----------------------------------------------------------------------------

/// Called when buttons are pressed or released
void buttonHandler(
    int button,     // button number (1,2)
    bool state,     // pressed (true) or released (false)
    double time     // time period since last state change
) {
    // reset the timer whenever the user interacts with the machine
    g_lastUsed.reset();

    switch ( button ) {
    case 1:
        if ( state ) {
            // button 1 pushed
	    cout << "gaggia: button 1 pressed, but there is no button 1!?";
            }
        else {
            // button 1 released
        }
        break;

    case 2:
        if ( state ) {
            // button 2 pushed
        } else {
            // button 2 released
            if ( time >= 1.0 ) {
                // button was held for 1 second, shut down the system
                cout << "gaggia: shutting down\n";
                g_halt = true;
                g_quit = true;
            } else {
                // button was pushed briefly, toggle boiler power
                g_regulator.setPower( !g_regulator.getPower() );
                cout << "gaggia: boiler "
                     << (g_regulator.getPower() ? "enabled" : "disabled")
                     << endl;
            }
        }
        break;
    }
}

//-----------------------------------------------------------------------------

/// very simplistic configuration file loader
bool loadConfig( std::string fileName )
{
	// open the file
	ifstream f( fileName.c_str() );
	if ( !f ) return false;

	do {
		// read key
		string key;
		f >> ws >> key;
		if ( !f ) break;

		// read value
		double value;
		f >> ws >> value;
		if ( !f ) break;

		// store key/value
		config[key] = value;
		//cout << key << " = " << value << endl;
	} while (true);

	f.close();

	return true;
}

//-----------------------------------------------------------------------------

std::string makeLogFileName()
{
	// get the time
	time_t now;
	time( &now );
	struct tm *info = localtime( &now );

	// format date and time into the filename
	char buffer[256];
	sprintf(
		buffer,
		"%02d%02d%02d-%02d%02d.csv",
		info->tm_year % 100,
		info->tm_mon+1,
		info->tm_mday,
		info->tm_hour,
		info->tm_min
	);

	// return the string
	return buffer;
}

//-----------------------------------------------------------------------------

int runController(
	bool interactive,
	const std::string & fileName
) {
    // if button 2 is pushed during initialisation, abort
    if ( g_inputs.getButton(2) ) {
        cerr << "gaggia: button 2 is down: aborting" << endl;
        return 1;
    }

	Display display;

	// open log file
	ofstream out( fileName.c_str() );
	if ( !out ) {
		cerr << "error: unable to open log file " << fileName << endl;
		return 1;
	}

	// read configuration file
	if ( !loadConfig( configFile ) ) {
		out << "error: failed to load configuration from "
			<< configFile << endl;
		return 1;
	}

    // read auto cut out time (the value in the file is in minutes,
    // and we convert to seconds here)
    g_autoPowerOff = config["autoPowerOff"] * 60.0;

	// read PID controller parameters from configuration
	// these are the proportional, integral, derivate coefficients and
	// the integrator limits
	const double kP = config["kP"];
	const double kI = config["kI"];
	const double kD = config["kD"];
	const double kMin = config["iMin"];
	const double kMax = config["iMax"];

	// set the PID controller parameters
	g_regulator.setPIDGains( kP, kI, kD );
	g_regulator.setIntegratorLimits( kMin, kMax );

	// target temperature in degrees centigrade
	double targetTemp = config["targetTemp"];
	g_regulator.setTargetTemperature( targetTemp );

	// time step in seconds
	double timeStep = config["timeStep"];
	g_regulator.setTimeStep( timeStep );

	// output parameters to log
	char buffer[512];
	sprintf(
		buffer,
		"P=%.4lf,I=%.4lf,D=%.4lf,iMin=%.3lf,iMax=%.3lf,"
		"targetTemp=%.3lf,timeStep=%.3lf",
		kP, kI, kD, kMin, kMax,
		targetTemp, timeStep
	);
	out << buffer << endl;

	if ( interactive )
		nonblock(1);

	// time step for user interface / display
	const double timeStepGUI = 0.25;

	// start time and next time step
	double start = getClock();
	double next  = start;

	// turn on the power and start the regulator (boiler will begin to heat)
	g_regulator.setPower( g_enableBoiler ).start();

	do {
		// next time step
		next += timeStepGUI;

		// in interactive mode, exit if any key is pressed
		if ( interactive && kbhit() ) break;

		// if asked to stop (e.g. via SIGINT)
		if ( g_quit ) break;

		// calculate elapsed time
		double elapsed = getClock() - start;

		// get the latest temperature reading
		double latestTemp = g_regulator.getTemperature();

		// get the latest boiler power level
		double powerLevel = g_regulator.getPowerLevel();

		// dump values to log file
		sprintf(
			buffer,
			"%.3lf,%.2lf,%.2lf",
			elapsed, powerLevel, latestTemp
		);
		out << buffer << endl;

		if (interactive) {
			printf( "%.2lf %.2lf\n", elapsed, latestTemp );
		}

		// update temperature display
		display.updateTemperature( latestTemp );

        // if auto cut out is enabled (greater than one second) and if too
        // much time has elapsed since the last user interaction, and the
        // timer is running, turn off the boiler as a precaution
        if (
            (g_autoPowerOff > 1.0) &&
            g_lastUsed.isRunning() &&
            (g_lastUsed.getElapsed() > g_autoPowerOff)
        ) {
            // switch off the boiler
            g_regulator.setPower( false );

            // stop the timer to prevent repeat triggering
            g_lastUsed.stop();

            // explanatory message
            cout << "gaggia: switched off power due to inactivity\n";
        }

		// sleep for remainder of time step
		double remain = next - getClock();;
		if ( remain > 0.0 )
			delayms( static_cast<int>(1.0E3 * remain) );
	} while (true);

	if ( interactive )
		nonblock(0);

	// turn the boiler off before we exit
	g_regulator.setPower( false );

	// if the halt button was pushed, halt the system
	if ( g_halt ) {
		system( "halt" );
	}

  	return 0;
}

//-----------------------------------------------------------------------------

int runTests()
{
    Temperature temperature;
    System system;
	Display display;

    cout << "temp: " <<
        (temperature.getDegrees(0) ? "ready" : "not ready")
        << endl;

    nonblock(1);

	// time step
	const double timeStep = 0.5;

	// start time and next time step
	double start = getClock();
	double next  = start;

    do {
        // calculate next time step
        next += timeStep;

		// if quit has been requested (e.g. via SIGINT)
		if ( g_quit ) break;

        if ( kbhit() ) {
			// get key
			char key = getchar();

			bool stop = false;
			switch ( tolower(key) ) {
			default:
				stop = true;
			}

			if (stop) break;
		}

        // read temperature sensor
        double temp = 0.0;
        temperature.getDegrees( &temp );

        // print sensor values
        printf(
			"%.2lfC\n",
			temp
		);

		// update temperature on display
		display.updateTemperature( temp );

		// sleep for remainder of time step
		double remain = next - getClock();;
		if ( remain > 0.0 )
			delayms( static_cast<int>(1.0E3 * remain) );
    } while (true);

    nonblock(0);

	return 0;
}

//-----------------------------------------------------------------------------

int main( int argc, char **argv )
{
	// hook SIGINT so we can exit gracefully
	if ( signal(SIGINT, signalHandler) == SIG_ERR ) {
		cerr << "gaggia: failed to hook SIGINT\n";
		return 1;
	}

	// hook SIGTERM so we can exit gracefully
	if ( signal(SIGTERM, signalHandler) == SIG_ERR ) {
		cerr << "gaggia: failed to hook SIGTERM\n";
		return 1;
	}

    // register notification handlers
    g_inputs.notifyRegister( &buttonHandler );

	if ( argc < 2 ) {
		cerr << "gaggia: expected a command\n";
		return 1;
	}

	const string command( argv[1] );

	bool interactive = false;

	for (int i=2; i<argc; ++i) {
		string option( argv[i] );
		if ( option == "-i" )
			interactive = true;
		else if ( option == "-d" ) {
            // disable the boiler
			g_enableBoiler = false;
		} else
			cerr << "gaggia: unexpected option\n";
	}

	if ( command == "stop" ) {
		Boiler boiler;
		boiler.powerOff();
		cout << "gaggia: turned off boiler SSR\n";
		return 0;
	} else if ( command == "start" ) {
		string fileName( makeLogFileName() );
		cout << "gaggia: starting controller (log=" << fileName << ")\n";
		return runController( interactive, filePath + fileName );
	} else if ( command == "test" ) {
		cout << "gaggia: test mode\n";
		return runTests();
	} else {
		cerr << "gaggia: unrecognised command (" << command << ")\n";
		return 1;
	}

	return 1;
}

//-----------------------------------------------------------------------------
