module(..., package.seeall)

function onload(image)
  image.xml_description([=[
<module>
  <name>consul</name>
  <description><para>The consul module pulls JSON stats from Hashicorp Consul health checks</para>
  </description>
  <loader>lua</loader>
  <object>noit.module.resmon</object>
  <checkconfig>

    <parameter name="url"
               required="required"
               default="http:///v1/health/state/any"
               allowed=".+">The Consul health check url to call, see consul docs: https://www.consul.io/docs/agent/http/health.html</parameter>
    <parameter name="port"
               required="optional"
               default="8500"
               allowed="\d+">The TCP port can be specified to overide the default of 8500.</parameter>
    <parameter name="check_name"
               required="optional"
               allowed=".+">The name of the Consul check.</parameter>
    <parameter name="service_name"
               required="optional"
               allowed=".+">The Consul service name to check.</parameter>
    <parameter name="consul_dc"
               required="optional"
               allowed=".+">The UserSpecifiedConsulDC to filter to.</parameter>
    <parameter name="service_blacklist"
               required="optional"
               allowed=".+">A comma separated list of service names to skip</parameter>
    <parameter name="node_blacklist"
               required="optional"
               allowed=".+">A comma separated list of node names to skip</parameter>
    <parameter name="check_name_blacklist"
               required="optional"
               allowed=".+">A comma separated list of check names to skip</parameter>

  </checkconfig>
  <examples>
    <example>
      <title>Checking health of consul services</title>
      <para>This example checks the health of consul servers service from the c1.int.foo node.</para>
      <programlisting><![CDATA[
      <noit>
        <modules>
          <loader image="lua" name="lua">
            <config><directory>/opt/reconnoiter/libexec/modules-lua/?.lua</directory></config>
          </loader>
          <module loader="lua" name="consul" object="noit.module.resmon"/>
        </modules>
        <checks>
            <check uuid="2503f08c-7a0f-11e3-9ba0-7cd1c3dcddf7" target="c1.int.foo" period="60000" timeout="10000" name="test.consul" module="consul">
             <config>
               <url>http://c1.int.foo:8500/v1/health/service/fabio</url>
               <port>8500</port>
               <service_name>fabio</service_name>
               <check_name>Service 'fabio' check</check_name>
             </config>
            </check>
        </checks>
      </noit>
    ]]></programlisting>
    </example>
  </examples>
</module>
]=]);
  return 0
end

-- http://lua-users.org/wiki/SplitJoin
function split(str, delim, maxNb)
  -- Eliminate bad cases...
  if string.find(str, delim) == nil then
    return { str }
  end
  if maxNb == nil or maxNb < 1 then
    maxNb = 0    -- No limit
  end
  local result = {}
  local pat = "(.-)" .. delim .. "()"
  local nb = 0
  local lastPos
  for part, pos in string.gfind(str, pat) do
    nb = nb + 1
    result[nb] = part
    lastPos = pos
    if nb == maxNb then
      break
    end
  end
  -- Handle the last field
  if nb ~= maxNb then
    result[nb + 1] = string.sub(str, lastPos)
  end
  return result
end

function escape(s)
  s = string.gsub(s, "([%%,:/&=+%c])", function (c)
                    return string.format("%%%02X", string.byte(c))
  end)
  s = string.gsub(s, " ", "+")
  return s
end

function fix_config(inconfig)
  local config = {}

  for k,v in pairs(inconfig) do config[k] = v end

  if not config.url then
    config.url = 'http:///v1/health/state/any'
  end
  if not config.port then
    config.port = 8500
  end

  if config.consul_dc ~= nil then
    config.url = config.url .. '?dc=' escape(config.consul_dc)
  end

  return config
end

function set_check_metric(check, name, type, value)
    if type == 'i' then
        check.metric_int32(name, value)
    elseif type == 'I' then
        check.metric_uint32(name, value)
    elseif type == 'l' then
        check.metric_int64(name, value)
    elseif type == 'L' then
        check.metric_uint64(name, value)
    elseif type == 'n' then
        check.metric_double(name, value)
    elseif type == 's' then
        check.metric_string(name, value)
    else
        check.metric(name, value)
    end
end

-- look for a "Status" key in o and add to count_table for that status
function count_status(o, count_table)
   for k, v in pairs(o) do
      if count_table ~= nil and k == "Status" then
        count_table[v] = count_table[v] + 1
        count_table[""] = count_table[""] + 1
        return
      end
   end
end

function json_metric(check, prefix, o, index)
   local cnt = 0
   if type(o) == "table" then
      for k, v in pairs(o) do
         local np
         if type(v) ~= "table" and prefix ~= nil then
            np = prefix .. '`' .. k
            set_check_metric(check, np, (k and string.find(k, "Index")) and 'L' or 's', v)
            cnt = cnt + 1
         end
      end
      return cnt
   end
   return cnt
end

function make_safe(s)
   local x, y = string.gsub(s, ' ', '_')
   x, y = string.gsub(x, '[^A-Za-z0-9_-]', '')
   return x
end

function first_to_upper(str)
    return (str:gsub("^%l", string.upper))
end


function sort_checks_by_field(tbl, sort_func, field)
  local keys = {}
  for key in pairs(tbl) do
    table.insert(keys, key)
  end

  table.sort(keys, function(a, b)
               return sort_func(tbl[a][field], tbl[b][field])
  end)

  return keys
end

function init_count_table(tbl)
  tbl[""] = 0
  tbl["passing"] = 0
  tbl["warning"] = 0
  tbl["critical"] = 0
end

function count_table_inited(tbl)
  return tbl["warning"] ~= nil
end


function json_to_metrics(check, doc, blacklists, headers)
    local services = 0
    check.available()
    local data = doc:document()
    if data ~= nil then
       if string.find(check.config.url, "/v1/health/node") then
          -- For node checks, we need to find the stanza that covers the requested check name
          -- then log out the metrics in that check.  We also have to create related count checks
          -- for the node, regardless of check name
          local np
          local count_tables = {}
          local node_count_table = {}
          local service_count_tables = {}

          local idx = 0
          for k,v in pairs(data) do
             mtev.log("debug", "Finding check name: " .. k .. "\n")
             if type(v) == "table" then
               if not check.config.service_name or check.config.service_name == v.ServiceName then
                 local a = v.ServiceName
                 if a == nil or a == "" then
                   a = "agent"
                 end
                 if not service_count_tables[a] then
                   service_count_tables[a] = {}
                   init_count_table(service_count_tables[a])
                 end
                 count_status(v, service_count_tables[a])
               end
               if not check.config.check_name or check.config.check_name == v.Name then
                 np = "node`" .. v.ServiceName

                 if (v.ServiceName == "") then
                   np = "agent"
                 end
                 np = np .. "`" .. make_safe(v.Name)
                 mtev.log("debug", "Prefix is: '" .. np .. "'\n")
                 if not count_tables[np] then
                   count_tables[np] = {}
                   init_count_table(count_tables[np])
                 end

                 services = services + json_metric(check, np .. '`' .. idx, v, idx)
                 count_status(v, count_tables[np])
                 idx = idx + 1
               end
             end
          end

          for k3, v3 in pairs(count_tables) do
            np = k3
            for k4, v4 in pairs(count_tables[np]) do
              set_check_metric(check, np .. "`Num" .. first_to_upper(k4) .. "Checks", 'L', v4)
              services = services + 1
            end
          end

          for k3, v3 in pairs(service_count_tables) do
            if k3 == "agent" then
              np = "agent"
            else
              np = "node" .. '`' .. make_safe(k3)
            end
            for k4, v4 in pairs(service_count_tables[k3]) do
              set_check_metric(check, np .. "`Num" .. first_to_upper(k4) .. "Services", 'L', v4)
              services = services + 1
            end
          end

       elseif string.find(check.config.url, "/v1/health/service") then

          -- For service checks, we need to find the Checks member and then find filter to the requested check
          -- and then find the metrics in that check.  We also have to create related count checks
          -- for the node, possibly filtered to check name.

          local np
          local check_count_table = {}
          local count_table = {}
          init_count_table(count_table)

          -- add all the checks from all the nodes to a single array which we can sort
          -- by check name
          local checks = {}
          local check_index = 0

          for k,v in pairs(data) do
            if type(v) == "table" then
              local node_address
              if v.Node ~= nil then
                -- capture the Node.Address
                node_address = v.Node.Address
              end
              if v.Checks ~= nil then
                for i, c in pairs(v.Checks) do
                  check_index = check_index + 1
                  if check.config.check_name == nil or
                  (check.config.check_name ~= nil and c.Name == check.config.check_name) then
                    c.Address = node_address
                    checks[check_index] = c
                  end
                end
              end
            end
          end

          local sorted_keys = sort_checks_by_field(checks, function(a, b) return a < b end, "Name")

          local idx = 0
          local last_check_name
          for _, key in ipairs(sorted_keys) do
            local c = checks[key]
            if c.Name == last_check_name then
              idx = idx + 1
            else
              idx = 0
            end
            last_check_name = c.Name

            local x = check_count_table[c.Name]
            if x == nil then
              check_count_table[c.Name] = {}
              init_count_table(check_count_table[c.Name])
              x = check_count_table[c.Name]
            end
            np = "service`checks`" .. make_safe(c.Name)
            services = services + json_metric(check, np .. '`' .. idx, c, idx)
            np = "service`checks`" .. c.Status .. "`" .. make_safe(c.Name)
            services = services + json_metric(check, np .. '`' .. idx, c, idx)
            count_status(c, x)
            count_status(c, count_table)
          end
          for k3, v3 in pairs(check_count_table) do
            np = "service`checks`" .. make_safe(k3)
            for k4,v4 in pairs(v3) do
              set_check_metric(check, np .. "`Num" .. first_to_upper(k4) .. "Checks", 'L', v4)
              services = services + 1
            end
          end

          for k3, v3 in pairs(count_table) do
             np = "service`checks"
             set_check_metric(check, np .. "`Num" .. first_to_upper(k3) .. "Checks", 'L', v3)
             services = services + 1
          end

       elseif string.find(check.config.url, "/v1/health/state") then
          -- For state checks, we need to find the stanza that covers the requested check name
          -- then log out the metrics in that check.  We also have to create related count checks
          -- for the state, regardless of check name
          local np
          local count_services_table = {}
          local count_checks_table = {}
          local checks = {}
          local check_index = 0
          init_count_table(count_services_table)
          init_count_table(count_checks_table)

          for k,v in pairs(data) do
            if type(v) == "table" then
              if not blacklists.node[v.Node]
                and not blacklists.service[v.ServiceName]
                and not blacklists.check[v.Name] then
                  check_index = check_index + 1
                  checks[check_index] = v
              else
                mtev.log("debug", "Check blacklisted: " .. v.ServiceName .. "\n")
              end
            end
          end

          local sorted_keys = sort_checks_by_field(checks, function(a, b) return a < b end, "CreateIndex")

          local idx = 0
          for _, key in ipairs(sorted_keys) do
            local c = checks[key]
            np = "check`" .. c.Status

            mtev.log("debug", "Prefix is: '" .. np .. "'\n")
            services = services + json_metric(check, np .. '`' .. idx, c, idx)
            if c.ServiceName == nil or c.ServiceName == "" then
              count_status(c, count_checks_table)
            else
              count_status(c, count_services_table)
            end
            idx = idx + 1
          end

          for k3, v3 in pairs(count_services_table) do
             np = "check"
             set_check_metric(check, np .. "`Num" .. first_to_upper(k3) .. "Services", 'L', v3)
             services = services + 1
          end

          for k3, v3 in pairs(count_checks_table) do
            np = "check"
            set_check_metric(check, np .. "`Num" .. first_to_upper(k3) .. "Checks", 'L', v3)
            services = services + 1
          end
       end
    end

    if headers["x-consul-lastcontact"] then
      local n = tonumber(headers["x-consul-lastcontact"])
      set_check_metric(check, "LastContact", 'n', n / 1000.0) -- convert to seconds
      services = services + 1
    end

    if headers["x-consul-knownleader"] then
      set_check_metric(check, "KnownLeader", 's', headers["x-consul-knownleader"])
      services = services + 1
    end

    if services > 0 then check.good() else check.bad() end
    check.status("services=" .. services)
end

function process(check, output, headers)
  local jsondoc = mtev.parsejson(output)

  local blacklists = {["node"] = {}, ["service"] = {}, ["check"] = {}}

  if check.config.service_blacklist then
    local s = split(check.config.service_blacklist, ',')
    for i,sb in pairs(s) do
      mtev.log("debug", "Blacklisting Service: " .. sb .. "\n")
      blacklists.service[sb] = 1
    end
  end

  if check.config.node_blacklist then
    local s = split(check.config.node_blacklist, ',')
    for i,sb in pairs(s) do
      blacklists.node[sb] = 1
    end
  end

  if check.config.check_name_blacklist then
    local s = split(check.config.check_name_blacklist, ',')
    for i,sb in pairs(s) do
      blacklists.check[sb] = 1
    end
  end


  json_to_metrics(check, jsondoc, blacklists, headers)
end
