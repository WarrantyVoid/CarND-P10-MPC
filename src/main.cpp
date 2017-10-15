#include <math.h>
#include <uWS/uWS.h>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include "Eigen-3.3/Eigen/Core"
#include "Eigen-3.3/Eigen/QR"
#include "MPC.h"
#include "json.hpp"

// for convenience
using json = nlohmann::json;

namespace
{
  // Checks if the SocketIO event has JSON data.
  // If there is data the JSON object in string format will be returned,
  // else the empty string "" will be returned.
  std::string hasData(std::string s)
  {
    auto found_null = s.find("null");
    auto b1 = s.find_first_of("[");
    auto b2 = s.find_last_of("]");
    if (found_null != std::string::npos)
    {
      return "";
    }
    else if (b1 != std::string::npos && b2 != std::string::npos)
    {
      return s.substr(b1, b2 - b1 + 1);
    }
    return "";
  }

  // Evaluate a polynomial.
  double polyEval(const Eigen::VectorXd &coeffs, double x)
  {
    double result = 0.0;
    for (int i = 0; i < coeffs.size(); i++)
    {
      result += coeffs[i] * pow(x, i);
    }
    return result;
  }

  // Fit a polynomial.
  // Adapted from
  // https://github.com/JuliaMath/Polynomials.jl/blob/master/src/Polynomials.jl#L676-L716
  Eigen::VectorXd polyFit(const Eigen::VectorXd &xvals, const Eigen::VectorXd &yvals, int order)
  {
    assert(xvals.size() == yvals.size());
    assert(order >= 1 && order <= xvals.size() - 1);
    Eigen::MatrixXd A(xvals.size(), order + 1);

    for (int i = 0; i < xvals.size(); i++)
    {
      A(i, 0) = 1.0;
    }

    for (int j = 0; j < xvals.size(); j++)
    {
      for (int i = 0; i < order; i++)
      {
        A(j, i + 1) = A(j, i) * xvals(j);
      }
    }

    Eigen::HouseholderQR<Eigen::MatrixXd> Q = A.householderQr();
    Eigen::VectorXd result = Q.solve(yvals);
    return result;
  }
}


//=================================================================================


int main()
{
  uWS::Hub h;
  MPC mpc(20, 5.0);
  static const int latencyMs = 100;

  h.onMessage([&mpc](uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length, uWS::OpCode opCode)
  {
    // "42" at the start of the message means there's a websocket message event.
    // The 4 signifies a websocket message
    // The 2 signifies a websocket event
    std::string sdata = std::string(data).substr(0, length);
    std::cout << sdata << std::endl;
    if (sdata.size() > 2 && sdata[0] == '4' && sdata[1] == '2')
    {
      std::string s = hasData(sdata);
      if (s != "")
      {
        auto j = json::parse(s);
        std::string event = j[0].get<std::string>();
        if (event == "telemetry")
        {
          json msgJson;

          // Fetch car state from msg
          Eigen::VectorXd state(6);
          state << j[1]["x"]
                 , j[1]["y"]
                 , j[1]["psi"]
                 , j[1]["speed"]
                 , 0.0
                 , 0.0;

          // Fetch planned path from message
          std::vector<double> px = j[1]["ptsx"];
          std::vector<double> py = j[1]["ptsy"];

          // Translate path to vehicle coord system
          assert(px.size() == py.size());
          Eigen::VectorXd refXPlanned(px.size());
          Eigen::VectorXd refYPlanned(py.size());
          for (int i = 0; i < static_cast<int>(px.size()); ++i)
          {
            refXPlanned[i] = cos(-state[2]) * (px[i] - state[0]) - sin(-state[2]) * (py[i] - state[1]);
            refYPlanned[i] = sin(-state[2]) * (px[i] - state[0]) + cos(-state[2]) * (py[i] - state[1]);
          }

          // Match 3rd order polynom
          Eigen::VectorXd refCoeffs = polyFit(refXPlanned, refYPlanned, 3);

          // Sample waypoints from reference polynom for display
          std::vector<double> refWaypointsX;
          std::vector<double> refWaypointsY;
          for (int x = 0; x < 100; x += 5)
          {
            refWaypointsX.push_back(x);
            refWaypointsY.push_back(polyEval(refCoeffs, x));
          }
          msgJson["next_x"] = refWaypointsX;
          msgJson["next_y"] = refWaypointsY;

          // Add errors to state
          state(4) = -polyEval(refCoeffs, 0);
          state(5) = 0.0;

          // Calculate actuations using mpc
          std::vector<Eigen::VectorXd> actuations(2);
          /*if (mpc.Solve(state, refCoeffs, actuations))
          {
            Eigen::VectorXd actuation = actuations[1];
            msgJson["steering_angle"] = actuation(6);
            msgJson["throttle"] = actuation(7);

            //Display the MPC predicted trajectory
            std::vector<double> mpcWaypointsX;
            std::vector<double> mpcWaypointsY;
            for (int t = 0; t < actuation.size(); ++t)
            {
              mpcWaypointsX.push_back(actuations[t](0));
               mpcWaypointsX.push_back(actuations[t](1));
              msgJson["mpc_x"] = mpcWaypointsX;
              msgJson["mpc_y"] = mpcWaypointsX;
            }
          }
          else*/
          {
            msgJson["steering_angle"] = 0.0;
            msgJson["throttle"] = 0.4;
            msgJson["mpc_x"] = refWaypointsX;
            msgJson["mpc_y"] = refWaypointsY;
          }


          auto msg = "42[\"steer\"," + msgJson.dump() + "]";
          // Latency
          // The purpose is to mimic real driving conditions where
          // the car does actuate the commands instantly.
          //
          // Feel free to play around with this value but should be to drive
          // around the track with 100ms latency.
          //
          // NOTE: REMEMBER TO SET THIS TO 100 MILLISECONDS BEFORE
          // SUBMITTING.
          //std::this_thread::sleep_for(std::chrono::milliseconds(latencyMs));
          ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
          std::cout << msg << std::endl;
        }
      }
      else
      {
        // Manual driving
        std::string msg = "42[\"manual\",{}]";
        ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
      }
    }
  });

  // We don't need this since we're not using HTTP but if it's removed the
  // program
  // doesn't compile :-(
  h.onHttpRequest([](uWS::HttpResponse *res, uWS::HttpRequest req, char *data, size_t, size_t)
  {
    const std::string s = "<h1>Hello world!</h1>";
    if (req.getUrl().valueLength == 1)
    {
      res->end(s.data(), s.length());
    }
    else
    {
      // i guess this should be done more gracefully?
      res->end(nullptr, 0);
    }
  });

  h.onConnection([&h](uWS::WebSocket<uWS::SERVER> ws, uWS::HttpRequest req)
  {
    std::cout << "Connected!!!" << std::endl;
  });

  h.onDisconnection([&h](uWS::WebSocket<uWS::SERVER> ws, int code, char *message, size_t length)
  {
    ws.close();
    std::cout << "Disconnected" << std::endl;
  });

  int port = 4567;
  if (h.listen(port))
  {
    std::cout << "Listening to port " << port << std::endl;
  }
  else
  {
    std::cerr << "Failed to listen to port" << std::endl;
    return -1;
  }
  h.run();
}
