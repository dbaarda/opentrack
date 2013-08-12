/*******************************************************************************
* FaceTrackNoIR    This program is a private project of the some enthusiastic  *
*                  gamers from Holland, who don't like to pay much for         *
*                  head-tracking.                                              *
*                                                                              *
* Copyright (C) 2012   Wim Vriend (Developing)                                 *
*                      Ron Hendriks (Researching and Testing)                  *
*                                                                              *
* Homepage:        http://facetracknoir.sourceforge.net/home/default.htm       *
*                                                                              *
* This program is free software; you can redistribute it and/or modify it      *
* under the terms of the GNU General Public License as published by the        *
* Free Software Foundation; either version 3 of the License, or (at your       *
* option) any later version.                                                   *
*                                                                              *
* This program is distributed in the hope that it will be useful, but          *
* WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY   *
* or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for  *
* more details.                                                                *
*                                                                              *
* You should have received a copy of the GNU General Public License along      *
* with this program; if not, see <http://www.gnu.org/licenses/>.               *
*******************************************************************************/
#include "tracker.h"
#include "facetracknoir.h"

/** constructor **/
Tracker::Tracker( FaceTrackNoIR *parent ) :
    confid(false),
    should_quit(false),
    do_center(false)
{
    // Retieve the pointer to the parent
    mainApp = parent;
    // Load the settings from the INI-file
    loadSettings();
}

Tracker::~Tracker()
{
}

static void get_curve(double pos, double& out, THeadPoseDOF& axis) {
    bool altp = (pos < 0) && axis.altp;
    if (altp) {
        out = axis.invert * axis.curveAlt.getValue(pos);
        axis.curve.setTrackingActive( false );
        axis.curveAlt.setTrackingActive( true );
    }
    else {
        out = axis.invert * axis.curve.getValue(pos);
        axis.curve.setTrackingActive( true );
        axis.curveAlt.setTrackingActive( false );
    }
    out += axis.zero;
}

/** QThread run method @override **/
void Tracker::run() {
    T6DOF current_camera;  // Used for filtering
    T6DOF target_camera;
    T6DOF new_camera;
    
    /** Direct Input variables **/
    T6DOF offset_camera;
    T6DOF gameoutput_camera;
    
    bool bTracker1Confid = false;
    bool bTracker2Confid = false;
    
    double newpose[6];
    double last_post_filter[6];
    
    forever
    {
        if (should_quit)
            break;

        for (int i = 0; i < 6; i++)
            newpose[i] = 0;

        //
        // The second tracker serves as 'secondary'. So if an axis is
        // written by the second tracker it CAN be overwritten by the
        // Primary tracker. This is enforced by the sequence below.
        //
        if (Libraries->pSecondTracker) {
            bTracker2Confid = Libraries->pSecondTracker->GiveHeadPoseData(newpose);
        }

        if (Libraries->pTracker) {
            bTracker1Confid = Libraries->pTracker->GiveHeadPoseData(newpose);
        }

        {
            QMutexLocker foo(&mtx);
            confid = bTracker1Confid || bTracker2Confid;
            
            if ( confid ) {
                for (int i = 0; i < 6; i++)
                    mainApp->axis(i).headPos = newpose[i];
            }
            
            //
            // If Center is pressed, copy the current values to the offsets.
            //
            if (do_center)  {
                //
                // Only copy valid values
                //
                if (confid) {
                    for (int i = 0; i < 6; i++)
                        offset_camera.axes[i] = mainApp->axis(i).headPos;
                }
                
                Tracker::do_center = false;
                
                if (Libraries->pTracker)
                    Libraries->pTracker->NotifyCenter();
                
                if (Libraries->pSecondTracker)
                    Libraries->pSecondTracker->NotifyCenter();
                
                if (Libraries->pFilter)
                    Libraries->pFilter->Initialize();
            }
            
            if (getTrackingActive()) {
                // get values
                for (int i = 0; i < 6; i++)
                    target_camera.axes[i] = mainApp->axis(i).headPos;
                
                // do the centering
                target_camera = target_camera - offset_camera;
                
                //
                // Use advanced filtering, when a filter was selected.
                //
                if (Libraries->pFilter) {
                    for (int i = 0; i < 6; i++)
                        last_post_filter[i] = gameoutput_camera.axes[i];
                    Libraries->pFilter->FilterHeadPoseData(current_camera.axes, target_camera.axes, new_camera.axes, last_post_filter);
                }
                else {
                    new_camera = target_camera;
                }
                
                for (int i = 0; i < 6; i++) {
                    get_curve(new_camera.axes[i], output_camera.axes[i], mainApp->axis(i));
                }
                
                //
                // Send the headpose to the game
                //
                if (Libraries->pProtocol) {
                    gameoutput_camera = output_camera;
                    Libraries->pProtocol->sendHeadposeToGame( gameoutput_camera.axes, newpose );  // degrees & centimeters
                }
            }
        }
        
        //for lower cpu load
        msleep(1);
    }

    for (int i = 0; i < 6; i++)
    {
        mainApp->axis(i).curve.setTrackingActive(false);
        mainApp->axis(i).curveAlt.setTrackingActive(false);
    }
}

//
// Get the raw headpose, so it can be displayed.
//
void Tracker::getHeadPose( double *data ) {
    QMutexLocker foo(&mtx);
    for (int i = 0; i < 6; i++)
    {
        data[i] = mainApp->axis(i).headPos;
    }
}

//
// Get the output-headpose, so it can be displayed.
//
void Tracker::getOutputHeadPose( double *data ) {
    QMutexLocker foo(&mtx);
    for (int i = 0; i < 6; i++)
        data[i] = output_camera.axes[i];
}

//
// Load the current Settings from the currently 'active' INI-file.
//
void Tracker::loadSettings() {
    qDebug() << "Tracker::loadSettings says: Starting ";
    QSettings settings("opentrack");  // Registry settings (in HK_USER)

    QString currentFile = settings.value(
        "SettingsFile",
        QCoreApplication::applicationDirPath() + "/Settings/default.ini").toString();
    QSettings iniFile(currentFile, QSettings::IniFormat);  // Application settings (in INI-file)

    iniFile.beginGroup("Tracking");
    
    qDebug() << "loadSettings says: iniFile = " << currentFile;
    
    const char* names2[] = {
        "zero_tx",
        "zero_ty",
        "zero_tz",
        "zero_rx",
        "zero_ry",
        "zero_rz"
    };
    
    for (int i = 0; i < 6; i++)
        mainApp->axis(i).zero = iniFile.value(names2[i], 0).toDouble();
    
    iniFile.endGroup();
}

void Tracker::setInvertAxis(Axis axis, bool invert) { mainApp->axis(axis).invert = invert?-1.0f:1.0f; }
