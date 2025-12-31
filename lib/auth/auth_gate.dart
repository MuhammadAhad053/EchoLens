import 'package:flutter/material.dart';
import 'package:firebase_auth/firebase_auth.dart';
import '../screens/home_screen.dart';
import '../screens/login_screen.dart';

class AuthGate extends StatelessWidget {
  const AuthGate({super.key});

  @override
  Widget build(BuildContext context) {
    return StreamBuilder<User?>(
      stream: FirebaseAuth.instance.authStateChanges(),
      builder: (context, snapshot) {
        if (snapshot.connectionState == ConnectionState.waiting) {
          return const Scaffold(
            backgroundColor: Color(0xFF1C1C1C),
            body: Center(child: CircularProgressIndicator(color: Color.fromARGB(255, 212, 160, 24))),
          );
        }
        if (snapshot.hasData) {
          return const GroundingSearchScreen();
        }
        return const LoginScreen();
      },
    );
  }
}