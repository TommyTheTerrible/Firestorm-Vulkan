/**
 * @file lltemplatemessagedispatcher.h
 * @brief LLTemplateMessageDispatcher class
 *
 * $LicenseInfo:firstyear=2009&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */


#include "lltemplatemessagedispatcher.h"

#include "llhttpnode.h"
#include "llhost.h"
#include "message.h"
#include "llsd.h"
#include "lltemplatemessagereader.h"

#include "nd/ndexceptions.h" // <FS:ND/> For ndxran

LLTemplateMessageDispatcher::LLTemplateMessageDispatcher(LLTemplateMessageReader &template_message_reader) :
    mTemplateMessageReader(template_message_reader)
{
}

void LLTemplateMessageDispatcher::dispatch(const std::string& msg_name,
                                           const LLSD& message,
                                           LLHTTPNode::ResponsePtr responsep)
{
    const LLSD::Binary& data = message["body"]["binary-template-data"].asBinary();
    auto size = data.size();
    if(size == 0)
    {
        return;
    }

    LLHost host;
    host = gMessageSystem->getSender();

    bool validate_message = mTemplateMessageReader.validateMessage(data.data(), static_cast<S32>(size), host, true);

    if (validate_message)
    {
        // <FS:ND> Handle invalid packets by throwing an exception and a graceful continue
        //      mTemplateMessageReader.readMessage(data.data(),host);
        // TommyTheTerrible - Add a queue for special complex messages (NETSTUTTER)
        try
        {
            mTemplateMessageReader.readMessage(data.data(),host);
        }
        catch (nd::exceptions::xran& ex)
        {
            LL_WARNS() << ex.what() << LL_ENDL;
        }
        // </FS:ND>
    }
    else
    {
        gMessageSystem->clearReceiveState();
    }
}

