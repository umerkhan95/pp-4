import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter/services.dart';
import 'screens/splash_screen.dart';
import 'screens/device_discovery_screen.dart';
import 'screens/device_control_screen.dart';
import 'screens/settings_screen.dart';
import 'providers/app_providers.dart';
import 'services/notification_service.dart';
import 'utils/app_theme.dart';
import 'utils/app_constants.dart';

void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  
  // Initialize notification service
  await NotificationService.initialize();
  
  // Set preferred orientations
  await SystemChrome.setPreferredOrientations([
    DeviceOrientation.portraitUp,
    DeviceOrientation.portraitDown,
  ]);
  
  // Set system UI overlay style
  SystemChrome.setSystemUIOverlayStyle(
    const SystemUiOverlayStyle(
      statusBarColor: Colors.transparent,
      statusBarIconBrightness: Brightness.dark,
      systemNavigationBarColor: Colors.white,
      systemNavigationBarIconBrightness: Brightness.dark,
    ),
  );
  
  runApp(
    const ProviderScope(
      child: PP4SmartPetCareApp(),
    ),
  );
}

class PP4SmartPetCareApp extends ConsumerWidget {
  const PP4SmartPetCareApp({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final themeMode = ref.watch(themeModeProvider);
    final appState = ref.watch(appStateProvider);
    
    return MaterialApp(
      title: AppConstants.appName,
      debugShowCheckedModeBanner: false,
      theme: AppTheme.lightTheme,
      darkTheme: AppTheme.darkTheme,
      themeMode: themeMode,
      home: _getInitialScreen(appState),
      routes: {
        '/splash': (context) => const SplashScreen(),
        '/discovery': (context) => const DeviceDiscoveryScreen(),
        '/control': (context) => const DeviceControlScreen(),
        '/settings': (context) => const SettingsScreen(),
      },
      builder: (context, child) {
        return MediaQuery(
          data: MediaQuery.of(context).copyWith(
            textScaler: TextScaler.linear(1.0), // Prevent text scaling
          ),
          child: child!,
        );
      },
    );
  }
  
  Widget _getInitialScreen(AppState appState) {
    switch (appState.currentScreen) {
      case AppScreen.splash:
        return const SplashScreen();
      case AppScreen.discovery:
        return const DeviceDiscoveryScreen();
      case AppScreen.control:
        return const DeviceControlScreen();
      case AppScreen.settings:
        return const SettingsScreen();
      default:
        return const SplashScreen();
    }
  }
}

// Global error handler
class GlobalErrorHandler extends ConsumerWidget {
  final Widget child;
  
  const GlobalErrorHandler({
    super.key,
    required this.child,
  });

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    ref.listen<AsyncValue<void>>(
      globalErrorProvider,
      (previous, next) {
        next.whenOrNull(
          error: (error, stackTrace) {
            _showErrorDialog(context, error.toString());
          },
        );
      },
    );
    
    return child;
  }
  
  void _showErrorDialog(BuildContext context, String error) {
    showDialog(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('Error'),
        content: Text(error),
        actions: [
          TextButton(
            onPressed: () => Navigator.of(context).pop(),
            child: const Text('OK'),
          ),
        ],
      ),
    );
  }
}
