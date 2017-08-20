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

// Include definitions for import and export for shared library
#include "littlexpconnect_global.h"

#include "logging/logginghandler.h"
#include "logging/loggingutil.h"
#include "settings/settings.h"
#include "xpconnect.h"
#include "fs/sc/simconnectdata.h"
#include "fs/sc/simconnectreply.h"
#include "gui/consoleapplication.h"

#include <QDebug>

extern "C" {
#include "XPLMPlugin.h"
#include "XPLMProcessing.h"
#include "XPLMDataAccess.h"
#include "XPLMUtilities.h"
}

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <QSharedMemory>
#include <QDataStream>
#include <QBuffer>

/*
 * This file contains the C functions needed by the XPLM API. All functionality will be delegated to
 * the singleton XpConnect.
 *
 * This is the only file which exports symbols
 */

using atools::logging::LoggingHandler;
using atools::logging::LoggingUtil;
using atools::settings::Settings;

float flightLoopCallback(float inElapsedSinceLastCall, float inElapsedTimeSinceLastFlightLoop, int inCounter,
                         void *inRefcon);

/* Application object for event queue in server thread */
static atools::gui::ConsoleApplication *app = nullptr;
static bool pluginRunning = false;

static QSharedMemory sharedMemory;
static const int SHARED_MEMORY_SIZE = 8196;

/* Called on simulator startup */
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
  app->setApplicationVersion("0.3.0.develop");

  // Initialize logging and force logfiles into the system or user temp directory
  LoggingHandler::initializeForTemp(Settings::getOverloadedPath(":/littlexpconnect/resources/config/logging.cfg"));
  LoggingUtil::logSystemInformation();
  LoggingUtil::logStandardPaths();

  // Pass plugin info to X-Plane
  strcpy(outName, "Little XpConnect");
  strcpy(outSig, "ABarthel.LittleXpConnect.Connect");
  strcpy(outDesc, "Connects Little Navmap to X-Plane.");

  // Create an instance here since it will be accessed from the main server thread
  Settings::instance();

  // Create object instance but do not start it yet
  // xpc::XpConnect::instance();

  sharedMemory.setKey("LittleXpConnect");
  if(!sharedMemory.create(SHARED_MEMORY_SIZE, QSharedMemory::ReadWrite))
  {
    qWarning() << "LittleXpConnect" << Q_FUNC_INFO << "Cannot create" << sharedMemory.errorString();

    if(!sharedMemory.attach(QSharedMemory::ReadWrite))
      qWarning() << "LittleXpConnect" << Q_FUNC_INFO << "Cannot attach" << sharedMemory.errorString();
    else
      qInfo() << "LittleXpConnect" << Q_FUNC_INFO << "Attached to" << sharedMemory.key()
              << "native" << sharedMemory.nativeKey();
  }
  else
    qInfo() << "LittleXpConnect" << Q_FUNC_INFO << "Created" << sharedMemory.key()
            << "native" << sharedMemory.nativeKey();

  // Always successfull
  return 1;
}

/* Called when simulator terminates */
PLUGIN_API void XPluginStop(void)
{
  // qDebug() << "LittleXpConnect" << Q_FUNC_INFO << "XpConnect shutdown";
  // xpc::XpConnect::shutdown();
  pluginRunning = false;

  if(!sharedMemory.detach())
    qWarning() << "Cannot detach" << sharedMemory.errorString() << "from" << sharedMemory.key()
               << "native" << sharedMemory.nativeKey();
  else
    qInfo() << "LittleXpConnect" << Q_FUNC_INFO << "Detached from" << sharedMemory.key()
            << "native" << sharedMemory.nativeKey();

  qDebug() << "LittleXpConnect" << Q_FUNC_INFO << "sync settings";
  Settings::instance().syncSettings();

  qDebug() << "LittleXpConnect" << Q_FUNC_INFO << "Logging shutdown";
  LoggingHandler::shutdown();

  qDebug() << "LittleXpConnect" << Q_FUNC_INFO << "Logging shutdown done";
}

/* Enable plugin - can be called more than once during a simulator session */
PLUGIN_API int XPluginEnable(void)
{
  qDebug() << "LittleXpConnect" << Q_FUNC_INFO;

  // Register callback into method - first call in five seconds
  XPLMRegisterFlightLoopCallback(flightLoopCallback, 1.f, nullptr);

  // Start all threads and the TCP server
  // xpc::XpConnect::instance().pluginEnable();

  pluginRunning = true;
  return 1;
}

/* Disable plugin - can be called more than once during a simulator session */
PLUGIN_API void XPluginDisable(void)
{
  qDebug() << "LittleXpConnect" << Q_FUNC_INFO;
  pluginRunning = false;

  // Unregister call back
  XPLMUnregisterFlightLoopCallback(flightLoopCallback, nullptr);

  // Shut down all threads and the TCP server
  // xpc::XpConnect::instance().pluginDisable();
}

/* called on special messages like aircraft loaded, etc. */
PLUGIN_API void XPluginReceiveMessage(XPLMPluginID inFromWho, long inMessage, void *inParam)
{
  Q_UNUSED(inFromWho);
  Q_UNUSED(inMessage);
  Q_UNUSED(inParam);
}

float flightLoopCallback(float inElapsedSinceLastCall, float inElapsedTimeSinceLastFlightLoop, int inCounter,
                         void *inRefcon)
{
  Q_UNUSED(inElapsedSinceLastCall);
  Q_UNUSED(inElapsedTimeSinceLastFlightLoop);
  Q_UNUSED(inCounter);
  Q_UNUSED(inRefcon);

  atools::fs::sc::SimConnectData data;
  if(xpc::XpConnect::fillSimConnectData(data))
  {
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    data.write(&buffer);

    if(buffer.size() > SHARED_MEMORY_SIZE)
      qWarning() << "LittleXpConnect" << Q_FUNC_INFO << "Data too large" << buffer.size() << ">" << SHARED_MEMORY_SIZE;
    else
    {
      if(sharedMemory.lock())
      {
        memcpy(sharedMemory.data(), buffer.data().constData(), static_cast<size_t>(buffer.size()));
        qDebug() << "Lock ok size" << buffer.size();
        sharedMemory.unlock();
      }
      else
        qInfo() << "LittleXpConnect" << Q_FUNC_INFO << "Cannot lock" << sharedMemory.key()
                << "native" << sharedMemory.nativeKey();
    }
  }

  return 1.f;

  // if(pluginRunning)
  //// Use provided object pointer since it is faster and return seconds to next activation
  // return static_cast<xpc::XpConnect *>(inRefcon)->flightLoopCallback(inElapsedSinceLastCall,
  // inElapsedTimeSinceLastFlightLoop, inCounter);
  // else
  // return 1.f;
}