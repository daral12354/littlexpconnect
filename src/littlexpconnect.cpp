/*****************************************************************************
* Copyright 2015-2017 Alexander Barthel albar965@mailbox.org
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*****************************************************************************/

#include "littlexpconnect.h"

#include <QDebug>

extern "C" {
#include "XPLMPlugin.h"
#include "XPLMProcessing.h"
#include "XPLMDataAccess.h"
#include "XPLMUtilities.h"
}

#include "logging/logginghandler.h"
#include "logging/loggingutil.h"
#include "xpconnect.h"
#include "fs/sc/simconnectdata.h"
#include "fs/sc/simconnectreply.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "gui/consoleapplication.h"

float flightLoopCallback(float inElapsedSinceLastCall, float inElapsedTimeSinceLastFlightLoop, int inCounter,
                         void *inRefcon);

/* Application object for event queue */
static atools::gui::ConsoleApplication *app = nullptr;

PLUGIN_API int XPluginStart(char *outName, char *outSig, char *outDesc)
{
  qDebug() << "LittleXpConnect" << Q_FUNC_INFO;

  // Register atools types so we can stream them
  qRegisterMetaType<atools::fs::sc::SimConnectData>();
  qRegisterMetaType<atools::fs::sc::SimConnectReply>();
  qRegisterMetaType<atools::fs::sc::WeatherRequest>();

  // Create application object which is needed for the server thread event queue
  int argc = 0;
  app = new atools::gui::ConsoleApplication(argc, nullptr);
  app->setApplicationName("Little XpConnect");
  app->setOrganizationName("ABarthel");
  app->setOrganizationDomain("abarthel.org");
  app->setApplicationVersion("0.0.1.develop");

  // Initialize logging and force logfiles into the system or user temp directory
  atools::logging::LoggingHandler::initializeForTemp(":/littlexpconnect/resources/config/logging.cfg");
  atools::logging::LoggingUtil::logSystemInformation();
  atools::logging::LoggingUtil::logStandardPaths();

  // Pass plugin info to X-Plane
  strcpy(outName, "Little XpConnect");
  strcpy(outSig, "ABarthel.LittleXpConnect.Connect");
  strcpy(outDesc, "Connects Little Navmap to X-Plane.");

  // Create object but do not start it yet
  xpc::XpConnect::instance();

  return 1;
}

PLUGIN_API void XPluginStop(void)
{
  qDebug() << "LittleXpConnect" << Q_FUNC_INFO;
  xpc::XpConnect::instance().shutdown();
  atools::logging::LoggingHandler::shutdown();
}

PLUGIN_API int XPluginEnable(void)
{
  qDebug() << "LittleXpConnect" << Q_FUNC_INFO;

  // Register callback into method and start all threads and the TCP server
  XPLMRegisterFlightLoopCallback(flightLoopCallback, 5.f, &xpc::XpConnect::instance());
  xpc::XpConnect::instance().pluginEnable();

  return 1;
}

PLUGIN_API void XPluginDisable(void)
{
  qDebug() << "LittleXpConnect" << Q_FUNC_INFO;

  // Unregister call back and shut down all threads and the server
  XPLMUnregisterFlightLoopCallback(flightLoopCallback, &xpc::XpConnect::instance());
  xpc::XpConnect::instance().pluginDisable();
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID inFromWho, long inMessage, void *inParam)
{
  // Pass to object
  xpc::XpConnect::instance().receiveMessage(inFromWho, inMessage, inParam);
}

float flightLoopCallback(float inElapsedSinceLastCall, float inElapsedTimeSinceLastFlightLoop, int inCounter,
                         void *inRefcon)
{
  // Use provided object pointer since it is faster
  return static_cast<xpc::XpConnect *>(inRefcon)->flightLoopCallback(inElapsedSinceLastCall,
                                                                     inElapsedTimeSinceLastFlightLoop, inCounter);
}

// =======================================================================================
// LittleXpConnectTest
// =======================================================================================

void LittleXpConnectTest::start()
{
  char name[1024], sig[1024], desc[1024];

  XPluginStart(name, sig, desc);
}

void LittleXpConnectTest::stop()
{
  XPluginStop();
}

void LittleXpConnectTest::enable()
{
  XPluginEnable();
}

void LittleXpConnectTest::disable()
{
  XPluginDisable();
}

void LittleXpConnectTest::callback()
{
  static int i = 0;
  flightLoopCallback(1.f, 0.1f, i++, nullptr);
}